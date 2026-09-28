#include "drm_backend.hpp"
#include "input_manager.hpp"
#include "cursor_renderer.hpp"
#include "compositor.hpp"
#include "desktop_shell.hpp"
#include "ipc_server.hpp"

#include <iostream>
#include <cmath>
#include <chrono>
#include <csignal>
#include <poll.h>

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

    // Tworzymy pierwsze okno na pulpicie
    auto win1 = std::make_shared<aqua::Window>(1, 240.0f, 140.0f, 840.0f, 520.0f, "Terminal");
    compositor.add_window(win1);

    static uint32_t next_win_id = 2;

    // Podpięcie zdarzeń gładzika / myszy
    input.set_pointer_callback([&cursor, &compositor, &shell](const aqua::PointerEvent& e) {
        if (e.dx != 0.0 || e.dy != 0.0) {
            cursor.move(static_cast<float>(e.dx), static_cast<float>(e.dy));
            compositor.handle_pointer_move(cursor.x(), cursor.y());
            shell.handle_pointer_move(cursor.x(), cursor.y());
        }
        if (e.button != 0) {
            if (e.is_button_press) {
                // Sprawdź czy kliknięto w Dock
                int clicked_icon = shell.handle_pointer_click(cursor.x(), cursor.y());
                if (clicked_icon >= 0) {
                    std::cout << "[Aqua Shell] Kliknieto w ikone Docka: " << clicked_icon << std::endl;
                    // Jeśli kliknięto w Terminal (indeks 2) lub Finder (0) - stwórz nowe okno!
                    float offset = (next_win_id % 5) * 40.0f;
                    auto new_win = std::make_shared<aqua::Window>(
                        next_win_id++, 280.0f + offset, 160.0f + offset, 760.0f, 480.0f, "New Window"
                    );
                    compositor.add_window(new_win);
                    return;
                }
            }
            compositor.handle_pointer_button(e.button, e.is_button_press, cursor.x(), cursor.y());
        }
    });

    // Podpięcie klawiatury: klawisze ESC (1) lub Q (16) zamykają serwer
    input.set_key_callback([](const aqua::KeyEvent& e) {
        if (e.is_press) {
            std::cout << "[Aqua Event] Klawisz nacisniety: " << e.key << std::endl;
            // 1 = KEY_ESC, 16 = KEY_Q
            if (e.key == 1 || e.key == 16) {
                std::cout << "[Aqua Event] Wcisnieto klawisz wyjscia. Zamykanie..." << std::endl;
                g_running = false;
            }
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
        uniform float u_time;

        void main() {
            vec3 col1 = vec3(0.08, 0.15, 0.35); // Granat macOS
            vec3 col2 = vec3(0.85, 0.25, 0.45); // Karmin
            vec3 col3 = vec3(0.12, 0.65, 0.75); // Błękit

            float wave1 = sin(uv.x * 3.0 + u_time * 0.8) * 0.5 + 0.5;
            float wave2 = cos(uv.y * 3.0 - u_time * 0.6) * 0.5 + 0.5;

            vec3 finalCol = mix(col1, col2, wave1);
            finalCol = mix(finalCol, col3, wave2 * 0.6);

            FragColor = vec4(finalCol, 1.0);
        }
    )";

    GLuint vs = compile_shader(GL_VERTEX_SHADER, vs_src);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, fs_src);
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);

    GLint time_loc = glGetUniformLocation(prog, "u_time");

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

    std::cout << "[Aqua] WindowServer dziala! Dotknij gladzika lub rusz mysza, aby sterowac kursorem macOS." << std::endl;
    std::cout << "[Aqua] Wcisnij klawisz Q lub ESC na klawiaturze laptopa, aby zakonczyc." << std::endl;

    aqua::IpcServer ipc;
    if (!ipc.initialize(&compositor, backend.egl_display())) {
        std::cerr << "[OSTRZEŻENIE] Inicjalizacja serwera IPC nie powiodla sie." << std::endl;
    }

    struct pollfd fds[3];
    fds[0].fd = backend.drm_fd();
    fds[0].events = POLLIN;

    fds[1].fd = input.fd();
    fds[1].events = POLLIN;

    fds[2].fd = ipc.server_fd();
    fds[2].events = POLLIN;

    bool needs_redraw = true;

    while (g_running) {
        // Renderuj nową klatkę tylko jeśli nie czekamy na zakończenie poprzedniego page-flipa
        if (needs_redraw && !backend.waiting_for_flip()) {
            auto now = std::chrono::steady_clock::now();
            float elapsed = std::chrono::duration<float>(now - start_time).count();

            // 1. Tło pulpitu
            glUseProgram(prog);
            glUniform1f(time_loc, elapsed);

            glBindVertexArray(vao);
            glDrawArrays(GL_TRIANGLES, 0, 6);

            // 2. Okna macOS, Cienie (Drop Shadow) i kontrolki Traffic Lights
            compositor.render();

            // 3. macOS Shell: Top Menu Bar oraz pływający Dock z animacją powiększania
            shell.render(cursor.x(), cursor.y(), elapsed);

            // 4. Kursor myszy macOS na samym wierzchu
            cursor.render();

            // 5. Wysłanie klatki do kontrolera KMS (asynchroniczny page-flip)
            if (!backend.start_page_flip()) {
                std::cerr << "[Aqua] Blad start_page_flip!" << std::endl;
                break;
            }

            needs_redraw = false;
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

        // Czekaj na zdarzenie (VSync, mysz/klawiatura LUB komunikat klienta IPC)
        int timeout_ms = backend.waiting_for_flip() ? 20 : 16;
        int ret = poll(fds, 3, timeout_ms);
        if (ret < 0 && errno == EINTR) continue;

        // Obsługa przerwania VSync z DRM
        if (fds[0].revents & POLLIN) {
            backend.process_drm_events();
            needs_redraw = true;
        }

        // Obsługa wejścia z touchpada/myszy/klawiatury
        if (fds[1].revents & POLLIN) {
            input.dispatch_events();
            needs_redraw = true;
        }

        // Obsługa komunikatów i klatek z aplikacji klienckich przez IPC
        if (fds[2].revents & POLLIN) {
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

    cursor.shutdown();
    input.shutdown();
    backend.shutdown();

    std::cout << "[Aqua] Zamknieto bezpiecznie." << std::endl;
    return 0;
}
