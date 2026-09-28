#include "status_icons.hpp"
#include <vector>
#include <cmath>
#include <iostream>

namespace aqua {

namespace {

GLuint create_texture_from_alpha_mask(const std::vector<uint8_t>& mask, int width, int height) {
    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, width, height, 0, GL_RED, GL_UNSIGNED_BYTE, mask.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return tex;
}

} // namespace

StatusIconRenderer::StatusIconRenderer() = default;

StatusIconRenderer::~StatusIconRenderer() {
    shutdown();
}

void StatusIconRenderer::generate_icon_textures() {
    const int N = ICON_RES; // 32x32

    // 1. LOGO AQUA: Elegancka zaokrąglona kropla wody z wycięciem
    std::vector<uint8_t> mask_logo(N * N, 0);
    for (int y = 0; y < N; ++y) {
        for (int x = 0; x < N; ++x) {
            float px = (x + 0.5f) / N * 2.0f - 1.0f; // [-1, 1]
            float py = (y + 0.5f) / N * 2.0f - 1.0f;
            
            // Kształt kropli
            float r_bottom = std::hypot(px, py - 0.2f);
            bool in_drop = false;
            if (py >= 0.2f && r_bottom <= 0.65f) in_drop = true;
            if (py < 0.2f) {
                float cone_w = (py + 0.85f) * 0.45f;
                if (std::abs(px) <= cone_w && py >= -0.85f) in_drop = true;
            }
            // Wewnętrzne wycięcie
            if (std::hypot(px, py - 0.25f) < 0.28f) in_drop = false;

            if (in_drop) mask_logo[y * N + x] = 255;
        }
    }
    tex_logo_ = create_texture_from_alpha_mask(mask_logo, N, N);

    // 2. WI-FI: Trzy łuki zasięgu + kropka
    std::vector<uint8_t> mask_wifi(N * N, 0);
    for (int y = 0; y < N; ++y) {
        for (int x = 0; x < N; ++x) {
            float px = (x + 0.5f) - 16.0f;
            float py = (y + 0.5f) - 24.0f; // Punkt bazowy na dole
            float dist = std::hypot(px, py);
            float angle = std::atan2(-py, px); // Kąt w górę

            bool in_sector = (angle >= 0.6f && angle <= (3.14159f - 0.6f));
            bool is_wifi = false;

            if (in_sector) {
                // Łuk zewnętrzny
                if (dist >= 17.0f && dist <= 20.0f) is_wifi = true;
                // Łuk środkowy
                if (dist >= 11.5f && dist <= 14.5f) is_wifi = true;
                // Łuk wewnętrzny
                if (dist >= 6.0f && dist <= 9.0f) is_wifi = true;
            }
            // Kropka bazowa
            if (dist <= 2.8f && py <= 0.0f) is_wifi = true;

            if (is_wifi) mask_wifi[y * N + x] = 255;
        }
    }
    tex_wifi_ = create_texture_from_alpha_mask(mask_wifi, N, N);

    // 3. SPOTLIGHT: Okrągła lupa z rączką pod kątem 45 stopni
    std::vector<uint8_t> mask_spotlight(N * N, 0);
    for (int y = 0; y < N; ++y) {
        for (int x = 0; x < N; ++x) {
            float px = (x + 0.5f) - 13.0f;
            float py = (y + 0.5f) - 13.0f;
            float r = std::hypot(px, py);

            bool is_lens = (r >= 7.0f && r <= 9.8f);
            // Rączka (linia x = y od (19, 19) do (26, 26))
            float rx = (x + 0.5f) - 21.0f;
            float ry = (y + 0.5f) - 21.0f;
            float u = (rx + ry) * 0.7071f;
            float v = (rx - ry) * 0.7071f;
            bool is_handle = (std::abs(v) <= 1.4f && u >= -1.0f && u <= 8.0f);

            if (is_lens || is_handle) mask_spotlight[y * N + x] = 255;
        }
    }
    tex_spotlight_ = create_texture_from_alpha_mask(mask_spotlight, N, N);

    // 4. CONTROL CENTER: Dwa zaokrąglone suwaki z kropkami (macOS SF Symbol)
    std::vector<uint8_t> mask_cc(N * N, 0);
    for (int y = 0; y < N; ++y) {
        for (int x = 0; x < N; ++x) {
            bool is_cc = false;
            // Górny suwak: track y in [7, 13], x in [5, 27]
            if (y >= 8 && y <= 12 && x >= 6 && x <= 26) {
                is_cc = true;
                // Kropka lewa
                if (std::hypot(x - 11, y - 10) <= 4.0f) is_cc = true;
            }
            // Dolny suwak: track y in [19, 23], x in [5, 27]
            if (y >= 19 && y <= 23 && x >= 6 && x <= 26) {
                is_cc = true;
                // Kropka prawa
                if (std::hypot(x - 21, y - 21) <= 4.0f) is_cc = true;
            }
            if (is_cc) mask_cc[y * N + x] = 255;
        }
    }
    tex_control_center_ = create_texture_from_alpha_mask(mask_cc, N, N);

    // 5. BATERIA: Zaokrąglony obrys z bolcem
    std::vector<uint8_t> mask_battery(N * N, 0);
    for (int y = 0; y < N; ++y) {
        for (int x = 0; x < N; ++x) {
            bool is_bat = false;
            // Obrys zewnętrzny [3..25, 9..23]
            if (x >= 4 && x <= 25 && y >= 9 && y <= 23) {
                // Zaokrąglenie rogów
                int corner_dx = std::max(0, std::max(6 - x, x - 23));
                int corner_dy = std::max(0, std::max(11 - y, y - 21));
                if (std::hypot(corner_dx, corner_dy) <= 2.2f) {
                    // Ramka zewnętrzna 1.5 px
                    if (x <= 5 || x >= 24 || y <= 10 || y >= 22) is_bat = true;
                    // Wypełnienie baterii (np. 80%)
                    if (x >= 7 && x <= 21 && y >= 12 && y <= 20) is_bat = true;
                }
            }
            // Bolec baterii [26..28, 13..19]
            if (x >= 26 && x <= 28 && y >= 13 && y <= 19) is_bat = true;

            if (is_bat) mask_battery[y * N + x] = 255;
        }
    }
    tex_battery_ = create_texture_from_alpha_mask(mask_battery, N, N);
}

bool StatusIconRenderer::initialize(uint32_t screen_width, uint32_t screen_height) {
    screen_w_ = screen_width;
    screen_h_ = screen_height;

    generate_icon_textures();

    const char* vs_src = R"(#version 300 es
        layout (location = 0) in vec4 aVertex; // x, y, u, v
        uniform vec2 u_screen_size;
        uniform vec4 u_rect;
        out vec2 TexCoord;

        void main() {
            vec2 pixel_pos = u_rect.xy + aVertex.xy * u_rect.zw;
            vec2 ndc = (pixel_pos / u_screen_size) * 2.0 - 1.0;
            ndc.y = -ndc.y;
            gl_Position = vec4(ndc, 0.0, 1.0);
            TexCoord = aVertex.zw;
        }
    )";

    const char* fs_src = R"(#version 300 es
        precision mediump float;
        in vec2 TexCoord;
        out vec4 FragColor;
        uniform sampler2D u_tex;
        uniform vec4 u_color;

        void main() {
            float alpha = texture(u_tex, TexCoord).r;
            if (alpha < 0.05) discard;
            FragColor = vec4(u_color.rgb, u_color.a * alpha);
        }
    )";

    auto compile_s = [](GLenum type, const char* src) -> GLuint {
        GLuint s = glCreateShader(type);
        glShaderSource(s, 1, &src, nullptr);
        glCompileShader(s);
        return s;
    };

    GLuint vs = compile_s(GL_VERTEX_SHADER, vs_src);
    GLuint fs = compile_s(GL_FRAGMENT_SHADER, fs_src);
    program_ = glCreateProgram();
    glAttachShader(program_, vs);
    glAttachShader(program_, fs);
    glLinkProgram(program_);
    glDeleteShader(vs);
    glDeleteShader(fs);

    u_screen_size_ = glGetUniformLocation(program_, "u_screen_size");
    u_rect_ = glGetUniformLocation(program_, "u_rect");
    u_color_ = glGetUniformLocation(program_, "u_color");
    u_tex_ = glGetUniformLocation(program_, "u_tex");

    float quad_vertices[] = {
        0.0f, 0.0f,  0.0f, 0.0f,
        1.0f, 0.0f,  1.0f, 0.0f,
        0.0f, 1.0f,  0.0f, 1.0f,
        0.0f, 1.0f,  0.0f, 1.0f,
        1.0f, 0.0f,  1.0f, 0.0f,
        1.0f, 1.0f,  1.0f, 1.0f
    };

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);

    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad_vertices), quad_vertices, GL_STATIC_DRAW);

    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(float), 0);
    glEnableVertexAttribArray(0);

    return true;
}

void StatusIconRenderer::update_screen_size(uint32_t screen_width, uint32_t screen_height) {
    screen_w_ = screen_width;
    screen_h_ = screen_height;
}

void StatusIconRenderer::draw_icon(StatusIconType type, float x, float y, float size_w, float size_h, uint32_t tint_rgba) {
    GLuint tex = 0;
    switch (type) {
        case StatusIconType::AquaLogo: tex = tex_logo_; break;
        case StatusIconType::Battery: tex = tex_battery_; break;
        case StatusIconType::Wifi: tex = tex_wifi_; break;
        case StatusIconType::Spotlight: tex = tex_spotlight_; break;
        case StatusIconType::ControlCenter: tex = tex_control_center_; break;
    }
    if (!tex) return;

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glUseProgram(program_);
    glUniform2f(u_screen_size_, static_cast<float>(screen_w_), static_cast<float>(screen_h_));

    float r = ((tint_rgba >> 24) & 0xFF) / 255.0f;
    float g = ((tint_rgba >> 16) & 0xFF) / 255.0f;
    float b = ((tint_rgba >> 8) & 0xFF) / 255.0f;
    float a = (tint_rgba & 0xFF) / 255.0f;
    glUniform4f(u_color_, r, g, b, a);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);

    glBindVertexArray(vao_);
    glUniform4f(u_rect_, x, y, size_w, size_h);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    glDisable(GL_BLEND);
}

void StatusIconRenderer::shutdown() {
    if (tex_logo_) { glDeleteTextures(1, &tex_logo_); tex_logo_ = 0; }
    if (tex_battery_) { glDeleteTextures(1, &tex_battery_); tex_battery_ = 0; }
    if (tex_wifi_) { glDeleteTextures(1, &tex_wifi_); tex_wifi_ = 0; }
    if (tex_spotlight_) { glDeleteTextures(1, &tex_spotlight_); tex_spotlight_ = 0; }
    if (tex_control_center_) { glDeleteTextures(1, &tex_control_center_); tex_control_center_ = 0; }

    if (vao_) { glDeleteVertexArrays(1, &vao_); vao_ = 0; }
    if (vbo_) { glDeleteBuffers(1, &vbo_); vbo_ = 0; }
    if (program_) { glDeleteProgram(program_); program_ = 0; }
}

} // namespace aqua
