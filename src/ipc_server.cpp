#include "ipc_server.hpp"
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>
#include <iostream>
#include <cstring>
#include <algorithm>
#include <GLES2/gl2ext.h>
#include <drm_fourcc.h>

namespace aqua {

#ifndef EGL_LINUX_DMA_BUF_EXT
#define EGL_LINUX_DMA_BUF_EXT 0x3270
#define EGL_LINUX_DRM_FOURCC_EXT 0x3271
#define EGL_DMA_BUF_PLANE0_FD_EXT 0x3272
#define EGL_DMA_BUF_PLANE0_OFFSET_EXT 0x3273
#define EGL_DMA_BUF_PLANE0_PITCH_EXT 0x3274
#endif

IpcServer::IpcServer() = default;

IpcServer::~IpcServer() {
    shutdown();
}

bool IpcServer::initialize(WindowCompositor* compositor, EGLDisplay egl_dpy, const std::string& socket_path) {
    compositor_ = compositor;
    egl_dpy_ = egl_dpy;
    socket_path_ = socket_path;

    ::unlink(socket_path_.c_str());

    server_fd_ = ::socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (server_fd_ < 0) {
        std::cerr << "[Aqua IPC BŁĄD] Nie mozna utworzyc gniazda UNIX: " << std::strerror(errno) << std::endl;
        return false;
    }

    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, socket_path_.c_str(), sizeof(addr.sun_path) - 1);

    if (::bind(server_fd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << "[Aqua IPC BŁĄD] bind() zakonczony niepowodzeniem: " << std::strerror(errno) << std::endl;
        ::close(server_fd_);
        server_fd_ = -1;
        return false;
    }

    if (::listen(server_fd_, 16) < 0) {
        std::cerr << "[Aqua IPC BŁĄD] listen() zakonczony niepowodzeniem: " << std::strerror(errno) << std::endl;
        ::close(server_fd_);
        server_fd_ = -1;
        return false;
    }

    // Ustawienie uprawnień dla gniazda, aby każdy użytkownik mógł się łączyć
    ::chmod(socket_path_.c_str(), 0666);

    std::cout << "[Aqua IPC] Serwer IPC aktywny na: " << socket_path_ << std::endl;
    return true;
}

void IpcServer::handle_new_connection() {
    int client_fd = ::accept4(server_fd_, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
    if (client_fd < 0) return;

    ClientConnection client;
    client.fd = client_fd;
    client.client_id = next_client_id_++;

    std::cout << "[Aqua IPC] Nowy klient polaczony (ID: " << client.client_id 
              << ", fd: " << client_fd << ")" << std::endl;

    clients_.push_back(std::move(client));
}

GLuint IpcServer::import_dmabuf_to_texture(int dmabuf_fd, uint32_t width, uint32_t height, uint32_t stride, uint32_t fourcc, EGLImageKHR& out_img) {
    PFNEGLCREATEIMAGEKHRPROC eglCreateImageKHR =
        reinterpret_cast<PFNEGLCREATEIMAGEKHRPROC>(eglGetProcAddress("eglCreateImageKHR"));
    PFNGLEGLIMAGETARGETTEXTURE2DOESPROC glEGLImageTargetTexture2DOES =
        reinterpret_cast<PFNGLEGLIMAGETARGETTEXTURE2DOESPROC>(eglGetProcAddress("glEGLImageTargetTexture2DOES"));

    if (!eglCreateImageKHR || !glEGLImageTargetTexture2DOES) {
        std::cerr << "[Aqua IPC BŁĄD] Brak rozszerzen EGL_image_dma_buf!" << std::endl;
        ::close(dmabuf_fd);
        return 0;
    }

    const EGLint attribs[] = {
        EGL_WIDTH, static_cast<EGLint>(width),
        EGL_HEIGHT, static_cast<EGLint>(height),
        EGL_LINUX_DRM_FOURCC_EXT, static_cast<EGLint>(fourcc),
        EGL_DMA_BUF_PLANE0_FD_EXT, dmabuf_fd,
        EGL_DMA_BUF_PLANE0_OFFSET_EXT, 0,
        EGL_DMA_BUF_PLANE0_PITCH_EXT, static_cast<EGLint>(stride),
        EGL_NONE
    };

    EGLImageKHR img = eglCreateImageKHR(egl_dpy_, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, nullptr, attribs);
    ::close(dmabuf_fd); // EGL duplikuje deskryptor, możemy bezpiecznie zamknąć swój

    if (img == EGL_NO_IMAGE_KHR) {
        std::cerr << "[Aqua IPC BŁĄD] eglCreateImageKHR zakonczone kodem: 0x" 
                  << std::hex << eglGetError() << std::dec << std::endl;
        return 0;
    }

    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, img);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    out_img = img;
    std::cout << "[Aqua IPC] Pomyślnie zaimportowano dma-buf: " << width << "x" << height 
              << " do tekstury GL ID: " << tex << std::endl;
    return tex;
}

void IpcServer::handle_client_message(ClientConnection& client) {
    MsgHeader hdr{};
    struct msghdr msg{};
    struct iovec iov[2];

    iov[0].iov_base = &hdr;
    iov[0].iov_len = sizeof(hdr);

    // Bufor pomocniczy dla SCM_RIGHTS (deskryptory plików dma-buf)
    union {
        struct cmsghdr cm;
        char control[CMSG_SPACE(sizeof(int))];
    } control_un;

    msg.msg_iov = iov;
    msg.msg_iovlen = 1;
    msg.msg_control = control_un.control;
    msg.msg_controllen = sizeof(control_un.control);

    ssize_t n = ::recvmsg(client.fd, &msg, 0);
    if (n <= 0) {
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return;
        // Klient rozłączony
        std::cout << "[Aqua IPC] Klient ID " << client.client_id << " rozlaczyl sie." << std::endl;
        if (client.window && compositor_) {
            compositor_->remove_window(client.window->id());
        }
        ::close(client.fd);
        client.fd = -1;
        return;
    }

    // Odczyt payloadu
    std::vector<char> payload(hdr.size);
    if (hdr.size > 0) {
        ::recv(client.fd, payload.data(), hdr.size, 0);
    }

    // Wyciągnięcie przekazanego deskryptora dma-buf
    int passed_fd = -1;
    struct cmsghdr* cmptr = CMSG_FIRSTHDR(&msg);
    if (cmptr && cmptr->cmsg_len == CMSG_LEN(sizeof(int)) &&
        cmptr->cmsg_level == SOL_SOCKET && cmptr->cmsg_type == SCM_RIGHTS) {
        passed_fd = *reinterpret_cast<int*>(CMSG_DATA(cmptr));
    }

    switch (hdr.type) {
        case MessageType::CreateWindow: {
            if (payload.size() < sizeof(MsgCreateWindow)) break;
            auto* req = reinterpret_cast<MsgCreateWindow*>(payload.data());
            uint32_t win_id = next_window_id_++;

            // Pozycja nowego okna klienta na ekranie
            float start_x = 220.0f + (win_id % 6) * 35.0f;
            float start_y = 120.0f + (win_id % 6) * 35.0f;
            auto win = std::make_shared<Window>(win_id, start_x, start_y, static_cast<float>(req->width), static_cast<float>(req->height), req->title);

            client.window_id = win_id;
            client.window = win;

            if (compositor_) {
                compositor_->add_window(win);
            }

            // Potwierdzenie dla klienta
            MsgHeader resp_hdr{MessageType::WindowCreated, 0, client.client_id, win_id};
            ::send(client.fd, &resp_hdr, sizeof(resp_hdr), 0);

            std::cout << "[Aqua IPC] Utworzono okno dla klienta: " << req->title 
                      << " (ID: " << win_id << ", " << req->width << "x" << req->height << ")" << std::endl;
            break;
        }

        case MessageType::AttachDmaBuf: {
            if (payload.size() < sizeof(MsgAttachDmaBuf) || passed_fd < 0) {
                if (passed_fd >= 0) ::close(passed_fd);
                break;
            }
            auto* req = reinterpret_cast<MsgAttachDmaBuf*>(payload.data());

            // Zwolnienie starej tekstury jeśli istniała
            if (client.texture_id) {
                glDeleteTextures(1, &client.texture_id);
                client.texture_id = 0;
            }
            if (client.egl_image != EGL_NO_IMAGE_KHR) {
                PFNEGLDESTROYIMAGEKHRPROC eglDestroyImageKHR =
                    reinterpret_cast<PFNEGLDESTROYIMAGEKHRPROC>(eglGetProcAddress("eglDestroyImageKHR"));
                if (eglDestroyImageKHR) eglDestroyImageKHR(egl_dpy_, client.egl_image);
                client.egl_image = EGL_NO_IMAGE_KHR;
            }

            // Import dma-buf klienta bezpośrednio do GPU kompozytora
            GLuint tex = import_dmabuf_to_texture(passed_fd, req->width, req->height, req->stride, req->drm_fourcc, client.egl_image);
            if (tex && client.window) {
                client.texture_id = tex;
                client.window->set_texture(tex);
            }
            break;
        }

        case MessageType::DestroyWindow: {
            if (client.window && compositor_) {
                compositor_->remove_window(client.window->id());
                client.window = nullptr;
            }
            break;
        }

        default:
            break;
    }
}

void IpcServer::dispatch_events() {
    handle_new_connection();

    for (auto& client : clients_) {
        if (client.fd >= 0) {
            handle_client_message(client);
        }
    }

    // Usuwanie rozłączonych klientów
    clients_.erase(
        std::remove_if(clients_.begin(), clients_.end(),
                       [](const auto& c) { return c.fd < 0; }),
        clients_.end()
    );
}

void IpcServer::shutdown() {
    for (auto& client : clients_) {
        if (client.fd >= 0) ::close(client.fd);
    }
    clients_.clear();

    if (server_fd_ >= 0) {
        ::close(server_fd_);
        server_fd_ = -1;
    }
    if (!socket_path_.empty()) {
        ::unlink(socket_path_.c_str());
    }
}

} // namespace aqua
