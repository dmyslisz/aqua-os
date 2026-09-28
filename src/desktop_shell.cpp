#include "desktop_shell.hpp"
#include "font_renderer.hpp"
#include <cmath>
#include <iostream>
#include <ctime>

namespace aqua {

namespace {

GLuint compile_shader(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    return s;
}

} // namespace

DesktopShell::DesktopShell() = default;

DesktopShell::~DesktopShell() {
    shutdown();
}

bool DesktopShell::initialize(uint32_t screen_width, uint32_t screen_height) {
    screen_w_ = screen_width;
    screen_h_ = screen_height;

    font_ = std::make_unique<FontRenderer>();
    if (!font_->initialize(screen_width, screen_height)) {
        std::cerr << "[Aqua Shell] Inicjalizacja FontRenderer nie powiodla sie." << std::endl;
    }

    // Ikony aplikacji macOS Dock
    dock_items_ = {
        {"Finder", "F", 0xFF2B88D9, true, DOCK_BASE_ICON_SIZE, 0.0f},
        {"Launchpad", "L", 0xFF85858A, false, DOCK_BASE_ICON_SIZE, 0.0f},
        {"Terminal", "T", 0xFF1C1C1E, true, DOCK_BASE_ICON_SIZE, 0.0f},
        {"Safari", "S", 0xFF007AFF, false, DOCK_BASE_ICON_SIZE, 0.0f},
        {"Code", "C", 0xFF0066B8, true, DOCK_BASE_ICON_SIZE, 0.0f},
        {"Music", "M", 0xFFFA2D55, false, DOCK_BASE_ICON_SIZE, 0.0f},
        {"Settings", "P", 0xFF8E8E93, false, DOCK_BASE_ICON_SIZE, 0.0f},
        {"Trash", "R", 0xFFD1D1D6, false, DOCK_BASE_ICON_SIZE, 0.0f}
    };

    const char* vs_src = R"(#version 300 es
        layout (location = 0) in vec2 aPos;
        uniform vec2 u_screen_size;
        uniform vec4 u_rect; // x, y, width, height

        out vec2 v_local_pos;

        void main() {
            vec2 pixel_pos = u_rect.xy + aPos * u_rect.zw;
            vec2 ndc = (pixel_pos / u_screen_size) * 2.0 - 1.0;
            ndc.y = -ndc.y;
            gl_Position = vec4(ndc, 0.0, 1.0);
            v_local_pos = aPos * u_rect.zw;
        }
    )";

    const char* fs_src = R"(#version 300 es
        precision highp float;
        in vec2 v_local_pos;
        out vec4 FragColor;

        uniform vec4 u_rect;
        uniform vec4 u_color;
        uniform vec4 u_border_color;
        uniform float u_radius;
        uniform int u_type; // 0 = Top Bar, 1 = Dock Body, 2 = Dock Icon, 3 = Active Dot

        float sdRoundedBox(vec2 p, vec2 b, float r) {
            vec2 q = abs(p) - b + vec2(r);
            return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r;
        }

        void main() {
            vec2 half_size = u_rect.zw * 0.5;
            vec2 p = v_local_pos - half_size;

            if (u_type == 0) {
                // Top Bar
                float d_shadow = v_local_pos.y - (u_rect.w - 1.0);
                if (d_shadow >= 0.0) {
                    FragColor = vec4(0.0, 0.0, 0.0, 0.20);
                } else {
                    FragColor = u_color;
                }
                return;
            }

            float d = sdRoundedBox(p, half_size, u_radius);
            if (d > 0.0) discard;

            float alpha = clamp(-d, 0.0, 1.0);
            vec4 col = u_color;

            if (d > -1.2) {
                col = mix(col, u_border_color, 0.75);
            }

            if (u_type == 2) {
                float grad = (v_local_pos.y / u_rect.w) * 0.15;
                col.rgb = clamp(col.rgb - vec3(grad), 0.0, 1.0);
            }

            FragColor = vec4(col.rgb, col.a * alpha);
        }
    )";

    GLuint vs = compile_shader(GL_VERTEX_SHADER, vs_src);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, fs_src);
    shell_program_ = glCreateProgram();
    glAttachShader(shell_program_, vs);
    glAttachShader(shell_program_, fs);
    glLinkProgram(shell_program_);
    glDeleteShader(vs);
    glDeleteShader(fs);

    u_screen_size_ = glGetUniformLocation(shell_program_, "u_screen_size");
    u_rect_ = glGetUniformLocation(shell_program_, "u_rect");
    u_color_ = glGetUniformLocation(shell_program_, "u_color");
    u_border_color_ = glGetUniformLocation(shell_program_, "u_border_color");
    u_radius_ = glGetUniformLocation(shell_program_, "u_radius");
    u_type_ = glGetUniformLocation(shell_program_, "u_type");

    float quad_vertices[] = {
        0.0f, 0.0f,
        1.0f, 0.0f,
        0.0f, 1.0f,
        0.0f, 1.0f,
        1.0f, 0.0f,
        1.0f, 1.0f
    };

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);

    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad_vertices), quad_vertices, GL_STATIC_DRAW);

    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    return true;
}

void DesktopShell::update_screen_size(uint32_t screen_width, uint32_t screen_height) {
    screen_w_ = screen_width;
    screen_h_ = screen_height;
    if (font_) font_->update_screen_size(screen_width, screen_height);
}

void DesktopShell::handle_pointer_move(float cursor_x, float cursor_y) {
    cursor_x_ = cursor_x;
    cursor_y_ = cursor_y;
}

int DesktopShell::handle_pointer_click(float cursor_x, float cursor_y) {
    float dock_y = screen_h_ - (DOCK_MAX_ICON_SIZE + 30.0f);
    if (cursor_y < dock_y) return -1;

    for (size_t i = 0; i < dock_items_.size(); ++i) {
        const auto& item = dock_items_[i];
        float base_ground_y = screen_h_ - 20.0f;
        float item_y = base_ground_y - item.current_size;
        if (cursor_x >= item.base_x && cursor_x <= (item.base_x + item.current_size) &&
            cursor_y >= item_y && cursor_y <= base_ground_y) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void DesktopShell::render_top_bar(float /*elapsed_time*/) {
    glUseProgram(shell_program_);
    glUniform2f(u_screen_size_, static_cast<float>(screen_w_), static_cast<float>(screen_h_));

    // 1. Tło paska menu (28 px wysokości)
    glUniform4f(u_rect_, 0.0f, 0.0f, static_cast<float>(screen_w_), TOP_BAR_HEIGHT);
    glUniform4f(u_color_, 0.96f, 0.96f, 0.97f, 0.88f);
    glUniform4f(u_border_color_, 0.0f, 0.0f, 0.0f, 0.15f);
    glUniform1f(u_radius_, 0.0f);
    glUniform1i(u_type_, 0);

    glBindVertexArray(vao_);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    // 2. Logo Apple / Aqua
    glUniform4f(u_rect_, 14.0f, 8.0f, 12.0f, 12.0f);
    glUniform4f(u_color_, 0.12f, 0.12f, 0.14f, 0.95f);
    glUniform1f(u_radius_, 3.0f);
    glUniform1i(u_type_, 2);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    // 3. Rysowanie wektorowego tekstu FreeType
    std::time_t t = std::time(nullptr);
    std::tm* now = std::localtime(&t);
    char time_str[32];
    std::strftime(time_str, sizeof(time_str), "%H:%M", now);

    if (font_) {
        // Nazwa aktywnej aplikacji
        font_->draw_text("Terminal", 36.0f, 6.0f, 0x1A1A1CFF);

        // Pozycje menu systemowego
        font_->draw_text("File    Edit    View    Window    Help", 130.0f, 6.0f, 0x3A3A3DFF);

        // Zegar po prawej stronie
        float time_w = font_->measure_text_width(time_str);
        font_->draw_text(time_str, static_cast<float>(screen_w_) - time_w - 20.0f, 6.0f, 0x1A1A1CFF);
    }
}

void DesktopShell::render_dock(float cursor_x, float cursor_y) {
    // Przywrócenie właściwego programu shaderów po rysowaniu czcionki
    glUseProgram(shell_program_);
    glUniform2f(u_screen_size_, static_cast<float>(screen_w_), static_cast<float>(screen_h_));

    const float base_icon_size = 48.0f;
    const float max_icon_size = 84.0f;
    const float padding_x = 12.0f;
    const float padding_y = 10.0f;
    const float spacing = 10.0f;
    const float bottom_margin = 10.0f;

    // Dół Docka i podstawa ikon są ZAKOTWICZONE (Anchor to bottom jak w macOS)
    float base_dock_h = base_icon_size + padding_y * 2.0f;
    float base_dock_top = screen_h_ - base_dock_h - bottom_margin;

    // Czy kursor znajduje się na Docku lub tuż nad nim
    bool is_over_dock = (cursor_y >= (base_dock_top - 15.0f)) && (cursor_y <= screen_h_);

    // Promień wpływu fali (dokładnie 2 sąsiadów z lewej i prawej jak w macOS 0:34)
    const float wave_radius = base_icon_size * 2.4f;
    const float PI = 3.1415926535f;

    float current_max_icon_h = base_icon_size;

    // 1. Obliczanie rozmiarów ikon (fala cosinusowa)
    for (auto& item : dock_items_) {
        float size = base_icon_size;
        if (is_over_dock) {
            float icon_center_x = item.base_x + item.current_size * 0.5f;
            float dist_x = std::abs(cursor_x - icon_center_x);

            if (dist_x < wave_radius) {
                // Cosine wave: 1.0 w centrum, gładki spadek do 0.0 na krawędzi
                float factor = 0.5f * (1.0f + std::cos((PI * dist_x) / wave_radius));
                size += (max_icon_size - base_icon_size) * factor;
            }
        }
        item.current_size = size;
        if (size > current_max_icon_h) {
            current_max_icon_h = size;
        }
    }

    // 2. Całkowita szerokość Docka
    float total_icons_w = 0.0f;
    for (size_t i = 0; i < dock_items_.size(); ++i) {
        total_icons_w += dock_items_[i].current_size;
        if (i + 1 < dock_items_.size()) total_icons_w += spacing;
    }

    float dock_w = total_icons_w + padding_x * 2.0f;
    // Wysokość Docka rośnie W GÓRĘ – dolna krawędź stoi w miejscu!
    float dock_h = current_max_icon_h + padding_y * 2.0f;
    float dock_x = (screen_w_ - dock_w) * 0.5f;
    float dock_y = screen_h_ - dock_h - bottom_margin;

    // 3. Rysowanie szklanej kapsuły Docka (Frosted Glass)
    glUniform4f(u_rect_, dock_x, dock_y, dock_w, dock_h);
    glUniform4f(u_color_, 0.95f, 0.95f, 0.97f, 0.68f);
    glUniform4f(u_border_color_, 1.0f, 1.0f, 1.0f, 0.85f);
    glUniform1f(u_radius_, 20.0f);
    glUniform1i(u_type_, 1);

    glBindVertexArray(vao_);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    // 4. Rysowanie ikon ZAKOTWICZONYCH DO DOLNEJ LINII
    // Podstawa każdej ikony spoczywa na: screen_h_ - bottom_margin - padding_y
    float base_ground_y = screen_h_ - bottom_margin - padding_y;

    float cur_x = dock_x + padding_x;
    for (auto& item : dock_items_) {
        item.base_x = cur_x;
        // Ikona rośnie wyłącznie w górę i na boki:
        float item_y = base_ground_y - item.current_size;

        float r = ((item.color >> 16) & 0xFF) / 255.0f;
        float g = ((item.color >> 8) & 0xFF) / 255.0f;
        float b = (item.color & 0xFF) / 255.0f;

        glUniform4f(u_rect_, cur_x, item_y, item.current_size, item.current_size);
        glUniform4f(u_color_, r, g, b, 1.0f);
        glUniform4f(u_border_color_, 1.0f, 1.0f, 1.0f, 0.45f);
        glUniform1f(u_radius_, item.current_size * 0.225f);
        glUniform1i(u_type_, 2);
        glDrawArrays(GL_TRIANGLES, 0, 6);

        // Kropka aktywnej aplikacji pod ikoną
        if (item.is_running) {
            float dot_size = 4.0f;
            float dot_x = cur_x + (item.current_size - dot_size) * 0.5f;
            float dot_y = base_ground_y + 3.0f;
            glUniform4f(u_rect_, dot_x, dot_y, dot_size, dot_size);
            glUniform4f(u_color_, 0.2f, 0.2f, 0.22f, 0.85f);
            glUniform1f(u_radius_, 2.0f);
            glUniform1i(u_type_, 3);
            glDrawArrays(GL_TRIANGLES, 0, 6);
        }

        cur_x += item.current_size + spacing;
    }
}

void DesktopShell::render(float cursor_x, float cursor_y, float elapsed_time) {
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // 1. Rysujemy Top Menu Bar
    render_top_bar(elapsed_time);

    // 2. Rysujemy pływający Dock z autentycznym powiększeniem ikon macOS
    render_dock(cursor_x, cursor_y);

    glDisable(GL_BLEND);
}

void DesktopShell::shutdown() {
    if (font_) { font_->shutdown(); font_.reset(); }
    if (vao_) { glDeleteVertexArrays(1, &vao_); vao_ = 0; }
    if (vbo_) { glDeleteBuffers(1, &vbo_); vbo_ = 0; }
    if (shell_program_) { glDeleteProgram(shell_program_); shell_program_ = 0; }
}

} // namespace aqua
