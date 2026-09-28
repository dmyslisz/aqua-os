#include "desktop_shell.hpp"
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

    // Definiujemy zestaw ikon w Docku w stylu macOS
    dock_items_ = {
        {"Finder", "F", 0xFF2B88D9, true, DOCK_BASE_ICON_SIZE, 0.0f},       // Klasyczny niebieski Finder
        {"Launchpad", "L", 0xFF85858A, false, DOCK_BASE_ICON_SIZE, 0.0f},   // Szary Launchpad
        {"Terminal", "T", 0xFF1C1C1E, true, DOCK_BASE_ICON_SIZE, 0.0f},     // Czarny Terminal z obwódką
        {"Safari", "S", 0xFF007AFF, false, DOCK_BASE_ICON_SIZE, 0.0f},      // Safari Błękit
        {"Code", "C", 0xFF0066B8, true, DOCK_BASE_ICON_SIZE, 0.0f},         // VS Code / Edytor
        {"Music", "M", 0xFFFA2D55, false, DOCK_BASE_ICON_SIZE, 0.0f},       // Różowe Music
        {"Settings", "P", 0xFF8E8E93, false, DOCK_BASE_ICON_SIZE, 0.0f},    // Szare Ustawienia
        {"Trash", "R", 0xFFD1D1D6, false, DOCK_BASE_ICON_SIZE, 0.0f}        // Kosz
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
                // TOP MENU BAR (Półprzezroczyste szkło macOS z cieniem u dołu)
                float d_shadow = v_local_pos.y - (u_rect.w - 1.0);
                if (d_shadow >= 0.0) {
                    // Cień pod paskiem menu
                    FragColor = vec4(0.0, 0.0, 0.0, 0.25);
                } else {
                    FragColor = u_color;
                }
                return;
            }

            // DOCK ORAZ IKONY (SDF z zaokrąglonymi rogami)
            float d = sdRoundedBox(p, half_size, u_radius);
            if (d > 0.0) discard;

            float alpha = clamp(-d, 0.0, 1.0);
            vec4 col = u_color;

            // Rysowanie subtelnej obwódki w stylu Apple Glass
            if (d > -1.2) {
                col = mix(col, u_border_color, 0.75);
            }

            // Dla ikon dodaj delikatny gradient pionowy (3D Squircle look)
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
}

void DesktopShell::handle_pointer_move(float cursor_x, float cursor_y) {
    cursor_x_ = cursor_x;
    cursor_y_ = cursor_y;
}

int DesktopShell::handle_pointer_click(float cursor_x, float cursor_y) {
    // Sprawdzamy czy kliknięto w ikonę w Docku
    float dock_y = screen_h_ - (DOCK_MAX_ICON_SIZE + DOCK_PADDING * 2.0f);
    if (cursor_y < dock_y) return -1;

    for (size_t i = 0; i < dock_items_.size(); ++i) {
        const auto& item = dock_items_[i];
        float item_y = (screen_h_ - DOCK_PADDING) - item.current_size;
        if (cursor_x >= item.base_x && cursor_x <= (item.base_x + item.current_size) &&
            cursor_y >= item_y && cursor_y <= (item_y + item.current_size)) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void DesktopShell::render_top_bar(float /*elapsed_time*/) {
    // 1. Rysowanie tła paska Top Bar (szerokość całego ekranu, 28 px)
    glUniform4f(u_rect_, 0.0f, 0.0f, static_cast<float>(screen_w_), TOP_BAR_HEIGHT);
    // macOS Półprzezroczyste szkło: białe o kryciu 72%
    glUniform4f(u_color_, 0.95f, 0.95f, 0.96f, 0.78f);
    glUniform4f(u_border_color_, 0.0f, 0.0f, 0.0f, 0.15f);
    glUniform1f(u_radius_, 0.0f);
    glUniform1i(u_type_, 0);

    glBindVertexArray(vao_);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    // 2. Akcenty na pasku (Logo  i nazwa aktywnej aplikacji: mały elegancki badge)
    glUniform4f(u_rect_, 14.0f, 6.0f, 16.0f, 16.0f);
    glUniform4f(u_color_, 0.15f, 0.15f, 0.16f, 0.9f); // Czarne logo
    glUniform1f(u_radius_, 4.0f);
    glUniform1i(u_type_, 2);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    // Badge aktywnej aplikacji "Terminal" (długi prostokącik)
    glUniform4f(u_rect_, 38.0f, 7.0f, 68.0f, 14.0f);
    glUniform4f(u_color_, 0.2f, 0.2f, 0.22f, 0.85f);
    glUniform1f(u_radius_, 3.0f);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    // Pozycje menu: File, Edit, View (delikatne szare prostokąciki symbolizujące tekst)
    float menu_x = 118.0f;
    float widths[] = { 32.0f, 36.0f, 40.0f, 54.0f };
    for (float w : widths) {
        glUniform4f(u_rect_, menu_x, 9.0f, w, 10.0f);
        glUniform4f(u_color_, 0.35f, 0.35f, 0.38f, 0.45f);
        glUniform1f(u_radius_, 2.0f);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        menu_x += w + 14.0f;
    }

    // Prawa strona paska menu: Zegar i wskaźniki statusu
    glUniform4f(u_rect_, screen_w_ - 90.0f, 7.0f, 76.0f, 14.0f);
    glUniform4f(u_color_, 0.22f, 0.22f, 0.24f, 0.75f);
    glUniform1f(u_radius_, 3.0f);
    glDrawArrays(GL_TRIANGLES, 0, 6);
}

void DesktopShell::render_dock(float cursor_x, float cursor_y) {
    // Obliczanie dynamicznego powiększania ikon (macOS Gaussian Magnification)
    float dock_center_y = screen_h_ - (DOCK_BASE_ICON_SIZE * 0.5f + DOCK_PADDING);
    float dist_to_dock_y = std::abs(cursor_y - dock_center_y);
    bool near_dock = dist_to_dock_y < 120.0f;

    float total_dock_width = DOCK_PADDING;
    const float sigma = 65.0f; // Szerokość fali powiększenia

    for (auto& item : dock_items_) {
        float size = DOCK_BASE_ICON_SIZE;
        if (near_dock) {
            float dist_x = cursor_x - (item.base_x + DOCK_BASE_ICON_SIZE * 0.5f);
            float factor = std::exp(-(dist_x * dist_x) / (2.0f * sigma * sigma));
            // Płynny spadek wraz z odległością w pionie
            float y_factor = std::max(0.0f, 1.0f - (dist_to_dock_y / 120.0f));
            size += (DOCK_MAX_ICON_SIZE - DOCK_BASE_ICON_SIZE) * factor * y_factor;
        }
        item.current_size = size;
        total_dock_width += size + DOCK_PADDING;
    }

    float dock_h = DOCK_MAX_ICON_SIZE * (near_dock ? 0.95f : 0.85f) + DOCK_PADDING * 1.5f;
    float dock_x = (screen_w_ - total_dock_width) * 0.5f;
    float dock_y = screen_h_ - dock_h - 10.0f; // 10 px odstępu od dolnej krawędzi (pływający dock)

    // 1. Rysowanie kapsuły Docka (Frosted Glass / Zaokrąglenie 20 px)
    glUniform4f(u_rect_, dock_x, dock_y, total_dock_width, dock_h);
    // Szklisty materiał macOS
    glUniform4f(u_color_, 0.92f, 0.92f, 0.94f, 0.58f);
    glUniform4f(u_border_color_, 1.0f, 1.0f, 1.0f, 0.65f); // Biały szklany obrys
    glUniform1f(u_radius_, 20.0f);
    glUniform1i(u_type_, 1);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    // 2. Rysowanie ikon aplikacji
    float cur_x = dock_x + DOCK_PADDING;
    for (auto& item : dock_items_) {
        item.base_x = cur_x;
        float item_y = (dock_y + dock_h - DOCK_PADDING * 0.8f) - item.current_size;

        // Wyciągamy kolory ikony
        float r = ((item.color >> 16) & 0xFF) / 255.0f;
        float g = ((item.color >> 8) & 0xFF) / 255.0f;
        float b = (item.color & 0xFF) / 255.0f;

        glUniform4f(u_rect_, cur_x, item_y, item.current_size, item.current_size);
        glUniform4f(u_color_, r, g, b, 1.0f);
        glUniform4f(u_border_color_, 1.0f, 1.0f, 1.0f, 0.35f);
        glUniform1f(u_radius_, item.current_size * 0.225f); // Zaokrąglony kształt Apple Squircle
        glUniform1i(u_type_, 2);
        glDrawArrays(GL_TRIANGLES, 0, 6);

        // Kropka aktywnej aplikacji (Running indicator) pod ikoną
        if (item.is_running) {
            float dot_size = 4.0f;
            float dot_x = cur_x + (item.current_size - dot_size) * 0.5f;
            float dot_y = dock_y + dock_h - 6.0f;
            glUniform4f(u_rect_, dot_x, dot_y, dot_size, dot_size);
            glUniform4f(u_color_, 0.2f, 0.2f, 0.22f, 0.8f);
            glUniform1f(u_radius_, 2.0f);
            glUniform1i(u_type_, 3);
            glDrawArrays(GL_TRIANGLES, 0, 6);
        }

        cur_x += item.current_size + DOCK_PADDING;
    }
}

void DesktopShell::render(float cursor_x, float cursor_y, float elapsed_time) {
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glUseProgram(shell_program_);
    glUniform2f(u_screen_size_, static_cast<float>(screen_w_), static_cast<float>(screen_h_));

    // 1. Rysujemy Top Menu Bar
    render_top_bar(elapsed_time);

    // 2. Rysujemy pływający Dock z powiększeniem ikon
    render_dock(cursor_x, cursor_y);

    glDisable(GL_BLEND);
}

void DesktopShell::shutdown() {
    if (vao_) { glDeleteVertexArrays(1, &vao_); vao_ = 0; }
    if (vbo_) { glDeleteBuffers(1, &vbo_); vbo_ = 0; }
    if (shell_program_) { glDeleteProgram(shell_program_); shell_program_ = 0; }
}

} // namespace aqua
