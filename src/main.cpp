#include "drm_backend.hpp"
#include <iostream>
#include <cmath>
#include <chrono>
#include <csignal>

static volatile bool g_running = true;

static void sigint_handler(int) {
    g_running = false;
}

// Prosty kompilator shaderów dla OpenGL ES 3.0
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
              << "  Aqua WindowServer (Quartz FreeBSD) - KROK 1\n"
              << "  Inicjalizacja sprzetowego KMS / GBM / EGL (Tiger Lake)\n"
              << "========================================================" << std::endl;

    aqua::DrmBackend backend;
    if (!backend.initialize("/dev/dri/card0")) {
        std::cerr << "Inicjalizacja backendu graficznego nie powiodla sie!" << std::endl;
        return 1;
    }

    std::cout << "[Aqua] Glowna petla renderowania uruchomiona. Nacisnij Ctrl+C, aby zakonczyc.\n"
              << "[Aqua] Rozdzielczosc: " << backend.width() << "x" << backend.height()
              << " @" << backend.refresh_rate() << "Hz" << std::endl;

    // Vertex shader dla prostokąta pełnoekranowego (Quad)
    const char* vs_src = R"(#version 300 es
        layout (location = 0) in vec2 aPos;
        out vec2 uv;
        void main() {
            uv = aPos * 0.5 + 0.5;
            gl_Position = vec4(aPos, 0.0, 1.0);
        }
    )";

    // Fragment shader z dynamicznym, animowanym gradientem (efekt macOS Sonoma / Big Sur)
    const char* fs_src = R"(#version 300 es
        precision highp float;
        in vec2 uv;
        out vec4 FragColor;
        uniform float u_time;

        void main() {
            // Dynamiczny gradient macOS Mesh Gradient
            vec3 col1 = vec3(0.08, 0.15, 0.35); // Gleboki granat
            vec3 col2 = vec3(0.85, 0.25, 0.45); // Karminowy roz
            vec3 col3 = vec3(0.12, 0.65, 0.75); // Morski turkus

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

    // Quad geometry (2 trójkąty pokrywające ekran)
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

    while (g_running) {
        auto now = std::chrono::steady_clock::now();
        float elapsed = std::chrono::duration<float>(now - start_time).count();

        // Renderowanie klatki
        glUseProgram(prog);
        glUniform1f(time_loc, elapsed);

        glBindVertexArray(vao);
        glDrawArrays(GL_TRIANGLES, 0, 6);

        // Zrzut na ekran i synchronizacja ze sprzętowym VSync
        if (!backend.swap_and_page_flip()) {
            std::cerr << "[Aqua] Blad przelaczania buforow!" << std::endl;
            break;
        }

        frame_count++;
        float fps_elapsed = std::chrono::duration<float>(now - last_fps_time).count();
        if (fps_elapsed >= 2.0f) {
            float fps = frame_count / fps_elapsed;
            std::cout << "[Aqua Metrics] Render FPS: " << fps 
                      << " (Stabilny VSync bez tearingu)" << std::endl;
            frame_count = 0;
            last_fps_time = now;
        }
    }

    std::cout << "\n[Aqua] Zamykanie serwera i przywracanie pierwotnego trybu konsoli..." << std::endl;
    glDeleteBuffers(1, &vbo);
    glDeleteVertexArrays(1, &vao);
    glDeleteProgram(prog);
    glDeleteShader(vs);
    glDeleteShader(fs);

    backend.shutdown();
    std::cout << "[Aqua] Zakonczono pomyslnie." << std::endl;
    return 0;
}
