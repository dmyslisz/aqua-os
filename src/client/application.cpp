#include "aqua/application.hpp"
#include "aqua_protocol.h"
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <poll.h>
#include <iostream>
#include <cstring>
#include <chrono>
#include <thread>
#include <algorithm>

namespace aqua {

Application* Application::s_instance_ = nullptr;

Application::Application(int argc, char* argv[], const std::string& app_name)
    : app_name_(app_name) {
    (void)argc;
    (void)argv;
    s_instance_ = this;
}

Application::~Application() {
    quit();
    if (sock_ >= 0) {
        ::close(sock_);
        sock_ = -1;
    }
    s_instance_ = nullptr;
}

bool Application::initialize(const std::string& socket_path) {
    sock_ = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock_ < 0) {
        std::cerr << "[libaqua BŁĄD] Nie mozna utworzyc gniazda UNIX: " << std::strerror(errno) << std::endl;
        return false;
    }

    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, socket_path.c_str(), sizeof(addr.sun_path) - 1);

    if (::connect(sock_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << "[libaqua BŁĄD] Nie mozna polaczyc sie z Aqua WindowServer (" 
                  << socket_path << "): " << std::strerror(errno) << std::endl;
        std::cerr << "Upewnij sie, ze 'sudo ./aqua-server' jest uruchomiony!" << std::endl;
        ::close(sock_);
        sock_ = -1;
        return false;
    }

    std::cout << "[libaqua] Polaczono aplikacje '" << app_name_ << "' z Aqua WindowServer!" << std::endl;
    return true;
}

void Application::add_window(std::shared_ptr<AppWindow> win) {
    if (!win || sock_ < 0) return;

    MsgCreateWindow req_create{};
    req_create.width = win->width();
    req_create.height = win->height();
    req_create.flags = 1; // FullSizeContent
    std::strncpy(req_create.title, win->title().c_str(), sizeof(req_create.title) - 1);

    MsgHeader hdr_create{MessageType::CreateWindow, sizeof(req_create), 0, 0};
    ::send(sock_, &hdr_create, sizeof(hdr_create), 0);
    ::send(sock_, &req_create, sizeof(req_create), 0);

    MsgHeader resp{};
    ssize_t n = ::recv(sock_, &resp, sizeof(resp), 0);
    if (n < static_cast<ssize_t>(sizeof(resp)) || resp.type != MessageType::WindowCreated) {
        std::cerr << "[libaqua BŁĄD] Nie udalo sie utworzyc okna w serwerze!" << std::endl;
        return;
    }

    if (!win->init_shm(sock_, resp.window_id)) {
        std::cerr << "[libaqua BŁĄD] Inicjalizacja bufora SHM dla okna nie powiodla sie!" << std::endl;
        return;
    }

    windows_.push_back(win);
}

std::shared_ptr<AppWindow> Application::find_window(uint32_t window_id) {
    for (auto& w : windows_) {
        if (w->id() == window_id) return w;
    }
    if (!windows_.empty()) return windows_.front();
    return nullptr;
}

void Application::dispatch_server_events() {
    struct pollfd pfd{sock_, POLLIN, 0};
    while (::poll(&pfd, 1, 0) > 0 && (pfd.revents & POLLIN)) {
        MsgHeader hdr{};
        ssize_t n = ::recv(sock_, &hdr, sizeof(hdr), MSG_DONTWAIT);
        if (n <= 0) {
            if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
            std::cout << "[libaqua] Polaczenie z serwerem zostalo przerwane." << std::endl;
            running_ = false;
            break;
        }

        std::vector<char> payload(hdr.size);
        if (hdr.size > 0) {
            size_t total = 0;
            while (total < hdr.size) {
                ssize_t r = ::recv(sock_, payload.data() + total, hdr.size - total, 0);
                if (r <= 0) break;
                total += r;
            }
        }

        auto win = find_window(hdr.window_id);

        switch (hdr.type) {
            case MessageType::WindowResized: {
                if (win && payload.size() >= sizeof(MsgWindowResized)) {
                    auto* res = reinterpret_cast<MsgWindowResized*>(payload.data());
                    win->handle_resize(sock_, res->width, res->height);
                }
                break;
            }
            case MessageType::PointerMotion: {
                if (win && payload.size() >= sizeof(MsgInputEvent)) {
                    auto* ev = reinterpret_cast<MsgInputEvent*>(payload.data());
                    win->on_mouse_move(ev->x, ev->y);
                }
                break;
            }
            case MessageType::PointerButton: {
                if (win && payload.size() >= sizeof(MsgInputEvent)) {
                    auto* ev = reinterpret_cast<MsgInputEvent*>(payload.data());
                    if (ev->state != 0) {
                        win->on_mouse_down(ev->x, ev->y, ev->code);
                    } else {
                        win->on_mouse_up(ev->x, ev->y, ev->code);
                    }
                }
                break;
            }
            case MessageType::KeyboardKey: {
                if (win && payload.size() >= sizeof(MsgInputEvent)) {
                    auto* ev = reinterpret_cast<MsgInputEvent*>(payload.data());
                    if (ev->state != 0) {
                        win->on_key_down(ev->code);
                    } else {
                        win->on_key_up(ev->code);
                    }
                }
                break;
            }
            case MessageType::WindowClosed: {
                if (win) {
                    win->on_close();
                    win->close();
                }
                break;
            }
            default:
                break;
        }
    }
}

int Application::run(int target_fps) {
    if (windows_.empty()) {
        std::cerr << "[libaqua] Brak okien do wyswietlenia." << std::endl;
        return 1;
    }

    running_ = true;
    auto frame_duration = std::chrono::milliseconds(1000 / std::max(1, target_fps));

    while (running_) {
        auto frame_start = std::chrono::steady_clock::now();

        dispatch_server_events();

        // Usun zamkniete okna
        windows_.erase(
            std::remove_if(windows_.begin(), windows_.end(),
                           [](const auto& w) { return w->should_close(); }),
            windows_.end()
        );

        if (windows_.empty()) {
            std::cout << "[libaqua] Wszystkie okna aplikacji zostaly zamkniete." << std::endl;
            break;
        }

        // Przerysuj okna wymagajace odswiezenia
        for (auto& win : windows_) {
            if (win->needs_redraw()) {
                win->render_and_commit(sock_);
            }
        }

        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - frame_start);
        if (elapsed < frame_duration) {
            int wait_ms = static_cast<int>((frame_duration - elapsed).count());
            struct pollfd pfd{sock_, POLLIN, 0};
            ::poll(&pfd, 1, wait_ms);
        }
    }

    return 0;
}

void Application::quit() {
    running_ = false;
}

} // namespace aqua
