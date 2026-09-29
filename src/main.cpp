#include "drm_backend.hpp"
#include "input_manager.hpp"
#include "cursor_renderer.hpp"
#include "compositor.hpp"
#include "desktop_shell.hpp"
#include "ipc_server.hpp"
#include "kawase_blur.hpp"

#include <iostream>
#include <cmath>
#include <chrono>
#include <csignal>
#include <poll.h>
#include <unistd.h>
#include <sys/wait.h>

static volatile bool g_running = true;

static void sigint_handler(int) {
    g_running = false;
}

GLuint compile_shader(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);

    GLint success = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &success);
    if (!success) {
        char log[512];
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        std::cerr << "[GL Shader BŁĄD] " << log << std::endl;
    }
    return s;
}

int main() {
    std::signal(SIGINT, sigint_handler);
    std::signal(SIGTERM, sigint_handler);
    std::signal(SIGCHLD, SIG_IGN); // Automatyczne sprzątanie procesów potomnych bez zombie

    std::cout << "========================================================\n"
              << "  Aqua WindowServer (Quartz FreeBSD) - KROK 4\n"
              << "  macOS Desktop Shell (Top Menu Bar & Animated Floating Dock)\n"
              << "========================================================" << std::endl;

    aqua::DrmBackend backend;
    if (!backend.initialize("/dev/dri/card0")) {
        std::cerr << "Inicjalizacja DRM/KMS zakonczona niepowodzeniem!" << std::endl;
        return 1;
    }

    aqua::InputManager input;
    if (!input.initialize()) {
        std::cerr << "[OSTRZEŻENIE] Inicjalizacja libinput nie powiodla sie w pelni." << std::endl;
    }

    aqua::CursorRenderer cursor;
    if (!cursor.initialize(backend.width(), backend.height())) {
        std::cerr << "Inicjalizacja kursora zakonczona niepowodzeniem!" << std::endl;
        return 1;
    }

    aqua::WindowCompositor compositor;
    if (!compositor.initialize(backend.width(), backend.height())) {
        std::cerr << "Inicjalizacja kompozytora zakonczona niepowodzeniem!" << std::endl;
        return 1;
    }

    aqua::DesktopShell shell;
    if (!shell.initialize(backend.width(), backend.height())) {
        std::cerr << "Inicjalizacja powłoki DesktopShell zakonczona niepowodzeniem!" << std::endl;
        return 1;
    }

    aqua::KawaseBlur blur;
    if (!blur.initialize(backend.width(), backend.height())) {
        std::cerr << "Inicjalizacja KawaseBlur nie powiodla sie!" << std::endl;
        return 1;
    }

    static uint32_t next_win_id = 1;

    // Podpięcie zdarzeń gładzika / myszy
    input.set_pointer_callback([&cursor, &compositor, &shell](const aqua::PointerEvent& e) {
        if (e.dx != 0.0 || e.dy != 0.0) {
            cursor.move(static_cast<float>(e.dx), static_cast<float>(e.dy));
            compositor.handle_pointer_move(cursor.x(), cursor.y());
            shell.handle_pointer_move(cursor.x(), cursor.y());
        }
        if (e.button != 0) {
            if (e.is_button_press) {
                int clicked_icon = shell.handle_pointer_click(cursor.x(), cursor.y());
                if (clicked_icon >= 0) {
                    std::cout << "[Aqua Dock] Kliknięto ikonę Docka: " << clicked_icon << std::endl;
                    shell.start_bounce(clicked_icon);

                    auto launch_or_focus = [&](const std::string& title_query, const char* exec_cmd) {
                        auto win = compositor.find_window_by_title(title_query);
                        if (win) {
                            std::cout << "[Aqua Server] Przywracanie okna na wierzch: " << title_query << std::endl;
                            compositor.bring_to_front(win->id());
                        } else if (exec_cmd) {
                            std::cout << "[Aqua Server] Uruchamianie aplikacji: " << exec_cmd << std::endl;
                            pid_t pid = fork();
                            if (pid == 0) {
                                execl(exec_cmd, exec_cmd, nullptr);
                                std::string base_name = exec_cmd;
                                if (base_name.rfind("./", 0) == 0) base_name = base_name.substr(2);
                                std::string alt1 = "build/" + base_name;
                                execl(alt1.c_str(), alt1.c_str(), nullptr);
                                std::string alt2 = "/home/dawid/aqua-os/build/" + base_name;
                                execl(alt2.c_str(), alt2.c_str(), nullptr);
                                execlp(base_name.c_str(), base_name.c_str(), nullptr);
                                std::cerr << "[Aqua Server] Błąd execl dla '" << exec_cmd << "': " << std::strerror(errno) << std::endl;
                                _exit(1);
                            }
                        }
                    };

                    switch (clicked_icon) {
                        case 0: { // Finder
                            auto win = compositor.find_window_by_title("Finder");
                            if (win) {
                                compositor.bring_to_front(win->id());
                            } else {
                                float offset = (next_win_id % 5) * 35.0f;
                                auto finder_win = std::make_shared<aqua::Window>(
                                    next_win_id++, 200.0f + offset, 120.0f + offset, 780.0f, 480.0f, "Finder"
                                );
                                compositor.add_window(finder_win);
                            }
                            break;
                        }
                        case 1: // Terminal (aqua-term z PTY i powłoką sh/zsh)
                            launch_or_focus("Terminal", "./aqua-term");
                            break;
                        case 2: // Calculator (aqua-calc)
                            launch_or_focus("Calculator", "./aqua-calc");
                            break;
                        case 3: // Activity Monitor (aqua-sysmon)
                            launch_or_focus("Activity Monitor", "./aqua-sysmon");
                            break;
                        case 4: // Visualizer (aqua-demo-client)
                            launch_or_focus("Demo", "./aqua-demo-client");
                            break;
                        case 5: { // Settings
                            auto win = compositor.find_window_by_title("Settings");
                            if (win) {
                                compositor.bring_to_front(win->id());
                            } else {
                                float offset = (next_win_id % 5) * 35.0f;
                                auto settings_win = std::make_shared<aqua::Window>(
                                    next_win_id++, 260.0f + offset, 150.0f + offset, 680.0f, 460.0f, "System Settings"
                                );
                                compositor.add_window(settings_win);
                            }
                            break;
                        }
                        case 6: // Trash
                            std::cout << "[Aqua Dock] Kosz otwarty (Empty Trash)" << std::endl;
                            break;
                    }
                    return;
                }
            }
            compositor.handle_pointer_button(e.button, e.is_button_press, cursor.x(), cursor.y());
        }
    });

    // Vertex & Fragment shader tła (macOS Mesh Gradient)
    const char* vs_src = R"(#version 300 es
        layout (location = 0) in vec2 aPos;
        out vec2 uv;
        void main() {
            uv = aPos * 0.5 + 0.5;
            gl_Position = vec4(aPos, 0.0, 1.0);
        }
    )";

    const char* fs_src = R"(#version 300 es
        precision highp float;
        in vec2 uv;
        out vec4 FragColor;

        void main() {
            // Statyczna, elegancka tapeta macOS Sequoia (Dark Glass Mesh)
            vec3 deep_space  = vec3(0.045, 0.055, 0.110); // Ciemny granat tła
            vec3 royal_blue  = vec3(0.090, 0.220, 0.580); // Szafir macOS
            vec3 sunset_rose = vec3(0.720, 0.190, 0.380); // Ciepły akcent zmierzchu
            vec3 soft_cyan   = vec3(0.120, 0.520, 0.680); // Błękit akcentu

            float d_top_right = distance(uv, vec2(0.85, 0.15));
            float d_bottom_left = distance(uv, vec2(0.18, 0.88));
            float d_center = distance(uv, vec2(0.48, 0.45));

            vec3 col = deep_space;
            col = mix(royal_blue, col, clamp(d_center * 1.05, 0.0, 1.0));
            col = mix(sunset_rose, col, smoothstep(0.05, 0.90, d_top_right) * 0.45);
            col = mix(soft_cyan, col, smoothstep(0.05, 0.85, d_bottom_left) * 0.35);

            // Subtelna winieta Apple na krawędziach ekranu
            float vignette = uv.x * (1.0 - uv.x) * uv.y * (1.0 - uv.y) * 16.0;
            vignette = clamp(pow(vignette, 0.22), 0.0, 1.0);
            col *= mix(0.80, 1.0, vignette);

            FragColor = vec4(col, 1.0);
        }
    )";

    GLuint vs = compile_shader(GL_VERTEX_SHADER, vs_src);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, fs_src);
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);

    float vertices[] = {
        -1.0f, -1.0f,
         1.0f, -1.0f,
        -1.0f,  1.0f,
        -1.0f,  1.0f,
         1.0f, -1.0f,
         1.0f,  1.0f
    };

    GLuint vbo, vao;
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);

    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    glViewport(0, 0, backend.width(), backend.height());

    auto start_time = std::chrono::steady_clock::now();
    uint64_t frame_count = 0;
    auto last_fps_time = start_time;

    aqua::IpcServer ipc;
    if (!ipc.initialize(&compositor, backend.egl_display())) {
        std::cerr << "[OSTRZEŻENIE] Inicjalizacja serwera IPC nie powiodla sie." << std::endl;
    }

    // Przekazywanie zdarzeń zmiany geometrii okna do klienta IPC
    compositor.set_resize_callback([&ipc](uint32_t win_id, uint32_t w, uint32_t h) {
        ipc.send_window_resized(win_id, w, h);
    });

    // Przekazywanie zdarzeń wskaźnika (myszy / touchpada) do klienta IPC
    compositor.set_input_callback([&ipc](uint32_t win_id, aqua::MessageType type, const aqua::MsgInputEvent& ev) {
        ipc.send_input_event(win_id, type, ev);
    });

    // Przekazywanie powiadomienia o zamknięciu okna przez Traffic Lights
    compositor.set_close_callback([&ipc](uint32_t win_id) {
        ipc.send_window_closed(win_id);
    });

    // Przekazywanie zdarzeń klawiatury do aktywnego okna
    static bool s_ctrl = false;
    static bool s_alt = false;

    input.set_key_callback([&compositor, &ipc](const aqua::KeyEvent& e) {
        if (e.key == 29 || e.key == 97) s_ctrl = e.is_press;
        if (e.key == 56 || e.key == 100) s_alt = e.is_press;

        // Awaryjne zamknięcie serwera graficznego: Ctrl + Alt + Backspace (14) lub Ctrl + Alt + Esc (1)
        if (e.is_press && s_ctrl && s_alt && (e.key == 14 || e.key == 1)) {
            std::cout << "[Aqua Event] Awaryjne wyjście (Ctrl+Alt+Backspace). Zamykanie serwera..." << std::endl;
            g_running = false;
            return;
        }

        uint32_t focused_id = compositor.focused_window_id();
        if (focused_id != 0) {
            aqua::MsgInputEvent ev{3, 0.0f, 0.0f, e.key, e.is_press ? 1u : 0u};
            ipc.send_input_event(focused_id, aqua::MessageType::KeyboardKey, ev);
        }
    });

    std::vector<struct pollfd> fds;
    fds.reserve(16);

    bool needs_redraw = true;

    while (g_running) {
        // Renderuj nową klatkę tylko jeśli nie czekamy na zakończenie poprzedniego page-flipa
        if (needs_redraw && !backend.waiting_for_flip()) {
            auto now = std::chrono::steady_clock::now();
            float elapsed = std::chrono::duration<float>(now - start_time).count();

            // KROK 1: Przechwycenie sceny (tapeta + okna) do bufora FBO
            blur.begin_scene();

            // 1a. Tło pulpitu (Statyczna elegancka tapeta macOS Sequoia)
            glUseProgram(prog);
            glBindVertexArray(vao);
            glDrawArrays(GL_TRIANGLES, 0, 6);

            // 1b. Okna macOS, Cienie (Drop Shadow) i kontrolki Traffic Lights
            compositor.render();

            blur.end_scene();

            // KROK 2: Wieloprzebiegowy Dual-Kawase Blur (tworzy aksamitną teksturę Frosted Glass)
            blur.process_blur();

            // KROK 3: Rysowanie ostrego tła sceny na ekran
            blur.draw_scene_to_screen();

            // KROK 4: macOS Shell (Top Menu Bar i Dock próbkujące rozmyte tło z blur.blurred_texture())
            // Synchronizacja stanu aktywnych aplikacji
            bool has_calc = (compositor.find_window_by_title("Calculator") != nullptr);
            bool has_sysmon = (compositor.find_window_by_title("Activity Monitor") != nullptr);
            bool has_visualizer = (compositor.find_window_by_title("Demo") != nullptr);
            bool has_terminal = (compositor.find_window_by_title("Terminal") != nullptr);
            bool has_finder = (compositor.find_window_by_title("Finder") != nullptr);
            bool has_settings = (compositor.find_window_by_title("Settings") != nullptr);

            shell.set_item_running("Calculator", has_calc);
            shell.set_item_running("Activity Monitor", has_sysmon);
            shell.set_item_running("Visualizer", has_visualizer);
            shell.set_item_running("Terminal", has_terminal);
            shell.set_item_running("Finder", has_finder);
            shell.set_item_running("Settings", has_settings);

            uint32_t focused_id = compositor.focused_window_id();
            if (focused_id != 0) {
                for (const auto& w : compositor.windows()) {
                    if (w->id() == focused_id) {
                        shell.set_active_app(w->title());
                        break;
                    }
                }
            } else {
                shell.set_active_app("Finder");
            }

            shell.render(cursor.x(), cursor.y(), elapsed, blur.blurred_texture());

            // KROK 5: Kursor myszy macOS na samym wierzchu
            cursor.render();

            // KROK 6: Wysłanie klatki do kontrolera KMS (asynchroniczny page-flip)
            if (!backend.start_page_flip()) {
                std::cerr << "[Aqua] Blad start_page_flip!" << std::endl;
                break;
            }

            needs_redraw = false;
            for (const auto& item : shell.dock_items()) {
                if (item.is_bouncing) {
                    needs_redraw = true;
                    break;
                }
            }
            frame_count++;

            float fps_elapsed = std::chrono::duration<float>(now - last_fps_time).count();
            if (fps_elapsed >= 3.0f) {
                float fps = frame_count / fps_elapsed;
                std::cout << "[Aqua Metrics] FPS: " << fps 
                          << " | Pozycja kursora: (" << cursor.x() << ", " << cursor.y() << ")" << std::endl;
                frame_count = 0;
                last_fps_time = now;
            }
        }

        // Przygotuj deskryptory dla poll():
        // [0] = DRM KMS (VSync)
        // [1] = Libinput
        // [2] = IPC Server (nowe połączenia)
        // [3..N] = Wszyscy podłączeni klienci IPC (dma-buf, SHM, klatki)
        fds.clear();
        fds.push_back({backend.drm_fd(), POLLIN, 0});
        fds.push_back({input.fd(), POLLIN, 0});
        fds.push_back({ipc.server_fd(), POLLIN, 0});
        for (const auto& c : ipc.clients()) {
            if (c.fd >= 0) {
                fds.push_back({c.fd, POLLIN, 0});
            }
        }

        int timeout_ms = backend.waiting_for_flip() ? 20 : 16;
        int ret = poll(fds.data(), static_cast<nfds_t>(fds.size()), timeout_ms);
        if (ret < 0 && errno == EINTR) continue;

        // Obsługa przerwania VSync z DRM
        if (fds.size() > 0 && (fds[0].revents & POLLIN)) {
            backend.process_drm_events();
            needs_redraw = true;
        }

        // Obsługa wejścia z touchpada/myszy/klawiatury
        if (fds.size() > 1 && (fds[1].revents & POLLIN)) {
            input.dispatch_events();
            needs_redraw = true;
        }

        // Obsługa komunikatów i klatek z IPC (nowi klienci oraz przesyłane bufory)
        bool ipc_activity = false;
        if (fds.size() > 2 && (fds[2].revents & POLLIN)) {
            ipc_activity = true;
        }
        for (size_t i = 3; i < fds.size(); ++i) {
            if (fds[i].revents & (POLLIN | POLLHUP | POLLERR)) {
                ipc_activity = true;
                break;
            }
        }

        if (ipc_activity) {
            ipc.dispatch_events();
            needs_redraw = true;
        }
    }

    std::cout << "\n[Aqua] Sprzatanie zasobow..." << std::endl;
    ipc.shutdown();
    glDeleteBuffers(1, &vbo);
    glDeleteVertexArrays(1, &vao);
    glDeleteProgram(prog);
    glDeleteShader(vs);
    glDeleteShader(fs);

    blur.shutdown();
    shell.shutdown();
    compositor.shutdown();
    cursor.shutdown();
    input.shutdown();
    backend.shutdown();

    std::cout << "[Aqua] Zamknieto bezpiecznie." << std::endl;
    return 0;
}
