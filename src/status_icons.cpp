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
    const int N = 64; // Wysoka rozdzielczość 64x64 dla nieskazitelnej ostrości Retina
    const int SAMPLES = 4; // 4x4 supersampling dla idealnego antyaliasingu krawędzi

    auto compute_mask = [&](auto sample_fn) -> std::vector<uint8_t> {
        std::vector<uint8_t> mask(N * N, 0);
        for (int y = 0; y < N; ++y) {
            for (int x = 0; x < N; ++x) {
                int hits = 0;
                for (int sy = 0; sy < SAMPLES; ++sy) {
                    for (int sx = 0; sx < SAMPLES; ++sx) {
                        float px = (x + (sx + 0.5f) / SAMPLES) / N * 2.0f - 1.0f;
                        float py = (y + (sy + 0.5f) / SAMPLES) / N * 2.0f - 1.0f;
                        if (sample_fn(px, py)) hits++;
                    }
                }
                mask[y * N + x] = static_cast<uint8_t>((hits * 255) / (SAMPLES * SAMPLES));
            }
        }
        return mask;
    };

    // 1. LOGO AQUA: Elegancka kropla
    auto mask_logo = compute_mask([](float px, float py) {
        float r = std::hypot(px, py - 0.2f);
        bool in_drop = false;
        if (py >= 0.2f && r <= 0.65f) in_drop = true;
        if (py < 0.2f && py >= -0.85f && std::abs(px) <= (py + 0.85f) * 0.48f) in_drop = true;
        if (std::hypot(px, py - 0.22f) < 0.26f) in_drop = false;
        return in_drop;
    });
    tex_logo_ = create_texture_from_alpha_mask(mask_logo, N, N);

    // 2. WI-FI: Trzy gładkie łuki zasięgu + kropka
    auto mask_wifi = compute_mask([](float px, float py) {
        float cx = px;
        float cy = py - 0.65f;
        float dist = std::hypot(cx, cy);
        float angle = std::atan2(-cy, cx);

        bool in_cone = (angle >= 0.72f && angle <= (3.14159f - 0.72f));
        if (in_cone) {
            if (dist >= 1.25f && dist <= 1.45f) return true; // Łuk 3
            if (dist >= 0.85f && dist <= 1.05f) return true; // Łuk 2
            if (dist >= 0.45f && dist <= 0.65f) return true; // Łuk 1
        }
        if (dist <= 0.18f && cy <= 0.0f) return true; // Kropka
        return false;
    });
    tex_wifi_ = create_texture_from_alpha_mask(mask_wifi, N, N);

    // 3. SPOTLIGHT: Lupa z diagramu Apple
    auto mask_spotlight = compute_mask([](float px, float py) {
        float r = std::hypot(px + 0.15f, py + 0.15f);
        if (r >= 0.42f && r <= 0.60f) return true; // Okrągła soczewka
        // Skośna rączka pod kątem 45 stopni
        float rx = px - 0.25f;
        float ry = py - 0.25f;
        float u = (rx + ry) * 0.7071f;
        float v = (rx - ry) * 0.7071f;
        if (std::abs(v) <= 0.08f && u >= 0.0f && u <= 0.55f) return true;
        return false;
    });
    tex_spotlight_ = create_texture_from_alpha_mask(mask_spotlight, N, N);

    // 4. CONTROL CENTRE: Dwa równoległe zaokrąglone suwaki z kropkami
    auto mask_cc = compute_mask([](float px, float py) {
        // Górny suwak: track py in [-0.55, -0.25], px in [-0.75, 0.75]
        if (py >= -0.52f && py <= -0.28f && std::abs(px) <= 0.70f) {
            if (std::hypot(px + 0.35f, py + 0.40f) <= 0.25f) return true; // Kropka lewa
            if (std::abs(px) <= 0.65f) return true;
        }
        // Dolny suwak: track py in [0.25, 0.55], px in [-0.75, 0.75]
        if (py >= 0.28f && py <= 0.52f && std::abs(px) <= 0.70f) {
            if (std::hypot(px - 0.35f, py - 0.40f) <= 0.25f) return true; // Kropka prawa
            if (std::abs(px) <= 0.65f) return true;
        }
        return false;
    });
    tex_control_center_ = create_texture_from_alpha_mask(mask_cc, N, N);

    // 5. BATERIA: Panoramiczna ramka z bolcem (jak w macOS)
    auto mask_battery = compute_mask([](float px, float py) {
        // Korpus: px in [-0.85, 0.65], py in [-0.42, 0.42]
        if (px >= -0.85f && px <= 0.65f && std::abs(py) <= 0.42f) {
            // Zewnętrzna ramka
            bool outer = true;
            // Wnętrze puste
            if (px >= -0.72f && px <= 0.52f && std::abs(py) <= 0.30f) {
                // Wypełnienie baterii zielonym/poziomem (np. 85%)
                if (px <= 0.34f) return true;
                return false;
            }
            return outer;
        }
        // Zewnętrzny bolec baterii po prawej stronie: px in [0.65, 0.80], py in [-0.18, 0.18]
        if (px >= 0.65f && px <= 0.80f && std::abs(py) <= 0.18f) return true;
        return false;
    });
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
