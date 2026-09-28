#include "drm_backend.hpp"
#include "input_manager.hpp"
#include "cursor_renderer.hpp"

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
              << "  Aqua WindowServer (Quartz FreeBSD) - KROK 2\n"
              << "  DRM/KMS + GBM + EGL + libinput (Gładzik/Klawiatura/Kursor)\n"
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

    // Podpięcie obsługi zdarzeń wskaźnika (gładzik / mysz)
    input.set_pointer_callback([&cursor](const aqua::PointerEvent& e) {
        if (e.dx != 0.0 || e.dy != 0.0) {
            cursor.move(static_cast<float>(e.dx), static_cast<float>(e.dy));
        }
        if (e.is_button_press) {
            std::cout << "[Aqua Event] Klikniecie przycisku myszy: " << e.button << std::endl;
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

    while (g_running) {
        // Przetworzenie zdarzeń gładzika i klawiatury
        input.dispatch_events();

        auto now = std::chrono::steady_clock::now();
        float elapsed = std::chrono::duration<float>(now - start_time).count();

        // 1. Renderowanie tła pulpitu
        glUseProgram(prog);
        glUniform1f(time_loc, elapsed);

        glBindVertexArray(vao);
        glDrawArrays(GL_TRIANGLES, 0, 6);

        // 2. Renderowanie nakładki kursora macOS
        cursor.render();

        // 3. Sprzętowa wymiana buforów i oczekiwanie na VSync
        if (!backend.swap_and_page_flip()) {
            std::cerr << "[Aqua] Blad swap_and_page_flip!" << std::endl;
            break;
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

    std::cout << "\n[Aqua] Sprzatanie zasobow..." << std::endl;
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
