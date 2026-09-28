#include "cursor_renderer.hpp"

#include <vector>
#include <cmath>
#include <iostream>

namespace aqua {

CursorRenderer::CursorRenderer() = default;

CursorRenderer::~CursorRenderer() {
    shutdown();
}

void CursorRenderer::generate_macos_cursor_texture() {
    // Generowanie bitmapy kursora macOS w rozmiarze 32x32 z alfa
    std::vector<uint32_t> pixels(CURSOR_TEX_SIZE * CURSOR_TEX_SIZE, 0x00000000);

    // Kształt strzałki macOS
    for (int y = 0; y < 24; ++y) {
        for (int x = 0; x < 18; ++x) {
            bool inside_black = false;
            bool inside_white_border = false;

            // Klasyczny profil strzałki
            if (x == 0 && y < 17) inside_white_border = true;
            if (x <= y && y < 17 && (x < (y * 0.7f + 1.0f))) inside_black = true;
            if (y >= 12 && y <= 22 && x >= 4 && x <= 8) {
                // Rączka strzałki
                int hx = x - 4;
                int hy = y - 12;
                if (hx == hy / 2 || hx == hy / 2 + 1) inside_black = true;
            }

            // Piksele kształtu kursora
            // Format RGBA8: Little-endian (AARRGGBB w uint32: AA=bity 24-31)
            int idx = y * CURSOR_TEX_SIZE + x;
            if (inside_black) {
                pixels[idx] = 0xFF1A1A1A; // Czerń macOS
            } else if (inside_white_border) {
                pixels[idx] = 0xFFFFFFFF; // Biały obrys
            }
        }
    }

    // Dodanie precyzyjnego białego obrysu wokół czarnych pikseli
    std::vector<uint32_t> smoothed = pixels;
    for (int y = 1; y < CURSOR_TEX_SIZE - 1; ++y) {
        for (int x = 1; x < CURSOR_TEX_SIZE - 1; ++x) {
            int idx = y * CURSOR_TEX_SIZE + x;
            if (pixels[idx] == 0) {
                // Sprawdź czy sąsiaduje z czarnym
                bool has_black_neighbor = false;
                for (int dy = -1; dy <= 1; ++dy) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        if (pixels[(y + dy) * CURSOR_TEX_SIZE + (x + dx)] == 0xFF1A1A1A) {
                            has_black_neighbor = true;
                            break;
                        }
                    }
                    if (has_black_neighbor) break;
                }
                if (has_black_neighbor) {
                    smoothed[idx] = 0xFFF0F0F0; // Czysty biały kontur
                }
            }
        }
    }

    glGenTextures(1, &texture_);
    glBindTexture(GL_TEXTURE_2D, texture_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, CURSOR_TEX_SIZE, CURSOR_TEX_SIZE, 0, GL_RGBA, GL_UNSIGNED_BYTE, smoothed.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

bool CursorRenderer::initialize(uint32_t screen_width, uint32_t screen_height) {
    screen_w_ = screen_width;
    screen_h_ = screen_height;
    x_ = screen_w_ / 2.0f;
    y_ = screen_h_ / 2.0f;

    generate_macos_cursor_texture();

    const char* vs_src = R"(#version 300 es
        layout (location = 0) in vec2 aPos;
        layout (location = 1) in vec2 aTex;
        out vec2 TexCoord;
        uniform vec2 u_pos;
        uniform vec2 u_screen_size;
        uniform vec2 u_cursor_size;

        void main() {
            // Przeliczenie pikseli ekranu na współrzędne NDC [-1, 1]
            vec2 pixel_pos = u_pos + aPos * u_cursor_size;
            vec2 ndc = (pixel_pos / u_screen_size) * 2.0 - 1.0;
            ndc.y = -ndc.y; // Odwrócenie osi Y dla współrzędnych ekranowych
            gl_Position = vec4(ndc, 0.0, 1.0);
            TexCoord = aTex;
        }
    )";

    const char* fs_src = R"(#version 300 es
        precision mediump float;
        in vec2 TexCoord;
        out vec4 FragColor;
        uniform sampler2D u_texture;

        void main() {
            vec4 col = texture(u_texture, TexCoord);
            if (col.a < 0.05) discard;
            FragColor = col;
        }
    )";

    auto compile_shader = [](GLenum type, const char* src) -> GLuint {
        GLuint s = glCreateShader(type);
        glShaderSource(s, 1, &src, nullptr);
        glCompileShader(s);
        return s;
    };

    GLuint vs = compile_shader(GL_VERTEX_SHADER, vs_src);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, fs_src);
    program_ = glCreateProgram();
    glAttachShader(program_, vs);
    glAttachShader(program_, fs);
    glLinkProgram(program_);
    glDeleteShader(vs);
    glDeleteShader(fs);

    pos_loc_ = glGetUniformLocation(program_, "u_pos");
    screen_size_loc_ = glGetUniformLocation(program_, "u_screen_size");
    cursor_size_loc_ = glGetUniformLocation(program_, "u_cursor_size");

    float quad_vertices[] = {
        // Pozycje [0, 1]   // Tekstura UV
        0.0f, 0.0f,         0.0f, 0.0f,
        1.0f, 0.0f,         1.0f, 0.0f,
        0.0f, 1.0f,         0.0f, 1.0f,
        0.0f, 1.0f,         0.0f, 1.0f,
        1.0f, 0.0f,         1.0f, 0.0f,
        1.0f, 1.0f,         1.0f, 1.0f,
    };

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);

    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad_vertices), quad_vertices, GL_STATIC_DRAW);

    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);

    return true;
}

void CursorRenderer::update_screen_size(uint32_t screen_width, uint32_t screen_height) {
    screen_w_ = screen_width;
    screen_h_ = screen_height;
}

void CursorRenderer::set_position(float x, float y) {
    x_ = std::max(0.0f, std::min(x, static_cast<float>(screen_w_)));
    y_ = std::max(0.0f, std::min(y, static_cast<float>(screen_h_)));
}

void CursorRenderer::move(float dx, float dy) {
    set_position(x_ + dx, y_ + dy);
}

void CursorRenderer::render() {
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glUseProgram(program_);
    glUniform2f(pos_loc_, x_, y_);
    glUniform2f(screen_size_loc_, static_cast<float>(screen_w_), static_cast<float>(screen_h_));
    glUniform2f(cursor_size_loc_, static_cast<float>(CURSOR_TEX_SIZE), static_cast<float>(CURSOR_TEX_SIZE));

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture_);

    glBindVertexArray(vao_);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    glDisable(GL_BLEND);
}

void CursorRenderer::shutdown() {
    if (vao_) { glDeleteVertexArrays(1, &vao_); vao_ = 0; }
    if (vbo_) { glDeleteBuffers(1, &vbo_); vbo_ = 0; }
    if (program_) { glDeleteProgram(program_); program_ = 0; }
    if (texture_) { glDeleteTextures(1, &texture_); texture_ = 0; }
}

} // namespace aqua
