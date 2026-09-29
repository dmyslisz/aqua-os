#include "desktop_shell.hpp"
#include "font_renderer.hpp"
#include "status_icons.hpp"
#include <cmath>
#include <iostream>
#include <ctime>
#include <algorithm>

namespace aqua {

namespace {

GLuint compile_shader(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    return s;
}

constexpr float DOCK_BOUNCE_PERIOD = 0.42f;

float compute_bounce_offset(float timer) {
    if (timer <= 0.0f) return 0.0f;
    float phase = std::fmod(timer, DOCK_BOUNCE_PERIOD) / DOCK_BOUNCE_PERIOD;
    return std::sin(phase * 3.14159265f) * 24.0f;
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

    status_icons_ = std::make_unique<StatusIconRenderer>();
    if (!status_icons_->initialize(screen_width, screen_height)) {
        std::cerr << "[Aqua Shell] Inicjalizacja StatusIconRenderer nie powiodla sie." << std::endl;
    }

    // Aplikacje z macOS Sequoia z dedykowanymi wektorowymi ikonami Retina
    dock_items_ = {
        {"Finder", StatusIconType::Finder, 0xFF1D74E8, true, DOCK_BASE_ICON_SIZE, 0.0f, false, 0.0f},
        {"Terminal", StatusIconType::Terminal, 0xFF202124, true, DOCK_BASE_ICON_SIZE, 0.0f, false, 0.0f},
        {"Calculator", StatusIconType::Calculator, 0xFF333336, false, DOCK_BASE_ICON_SIZE, 0.0f, false, 0.0f},
        {"Activity Monitor", StatusIconType::Sysmon, 0xFF182838, false, DOCK_BASE_ICON_SIZE, 0.0f, false, 0.0f},
        {"Visualizer", StatusIconType::Visualizer, 0xFF632B94, false, DOCK_BASE_ICON_SIZE, 0.0f, false, 0.0f},
        {"Settings", StatusIconType::Settings, 0xFF8E8E93, false, DOCK_BASE_ICON_SIZE, 0.0f, false, 0.0f},
        {"Trash", StatusIconType::Trash, 0xFFE0E0E6, false, DOCK_BASE_ICON_SIZE, 0.0f, false, 0.0f}
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

        uniform vec2 u_screen_size;
        uniform vec4 u_rect;
        uniform vec4 u_color;
        uniform vec4 u_border_color;
        uniform float u_radius;
        uniform int u_type; // 0 = Top Bar, 1 = Dock Glass Body, 2 = Dock Icon, 3 = Active Dot, 4 = Tooltip Badge
        uniform sampler2D u_blur_tex;
        uniform int u_has_blur;

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
                    FragColor = vec4(0.0, 0.0, 0.0, 0.18);
                } else {
                    if (u_has_blur == 1) {
                        vec2 screen_pos = u_rect.xy + v_local_pos;
                        vec2 screen_uv = vec2(screen_pos.x / u_screen_size.x, 1.0 - (screen_pos.y / u_screen_size.y));
                        vec3 blurred = texture(u_blur_tex, screen_uv).rgb;
                        // macOS Frosted Glass: mleczne zabarwienie matowego szkła
                        vec3 frosted = mix(blurred, vec3(0.96, 0.96, 0.98), 0.45);
                        FragColor = vec4(frosted, 0.92);
                    } else {
                        FragColor = u_color;
                    }
                }
                return;
            }

            float d = sdRoundedBox(p, half_size, u_radius);
            if (d > 0.0) discard;

            float alpha = clamp(-d, 0.0, 1.0);
            vec4 col = u_color;

            if (u_type == 1 && u_has_blur == 1) {
                vec2 screen_pos = u_rect.xy + v_local_pos;
                vec2 screen_uv = vec2(screen_pos.x / u_screen_size.x, 1.0 - (screen_pos.y / u_screen_size.y));
                vec3 blurred = texture(u_blur_tex, screen_uv).rgb;
                // macOS Sequoia Glass Tint: jasne, krystalicznie matowe szkło
                vec3 frosted = mix(blurred, vec3(0.97, 0.97, 0.99), 0.50);
                col = vec4(frosted, 0.82);
            }

            // macOS 15 Glass Border
            if (d > -1.2) {
                col = mix(col, u_border_color, 0.85);
            }

            // Ikony: zaokrąglony Squircle z subtelnym oświetleniem od góry
            if (u_type == 2) {
                float light = (1.0 - (v_local_pos.y / u_rect.w)) * 0.12;
                col.rgb = clamp(col.rgb + vec3(light), 0.0, 1.0);
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
    u_blur_tex_ = glGetUniformLocation(shell_program_, "u_blur_tex");
    u_has_blur_ = glGetUniformLocation(shell_program_, "u_has_blur");

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

void DesktopShell::start_bounce(int index) {
    if (index >= 0 && index < static_cast<int>(dock_items_.size())) {
        dock_items_[index].is_bouncing = true;
        dock_items_[index].bounce_timer = 0.0f;
    }
}

void DesktopShell::set_item_running(const std::string& name, bool running) {
    for (auto& item : dock_items_) {
        if (item.name == name || item.name.find(name) != std::string::npos) {
            item.is_running = running;
        }
    }
}

int DesktopShell::handle_pointer_click(float cursor_x, float cursor_y) {
    float dock_base_y = screen_h_ - 68.0f;
    if (cursor_y < (dock_base_y - 60.0f) || cursor_y > screen_h_) return -1;

    for (size_t i = 0; i < dock_items_.size(); ++i) {
        const auto& item = dock_items_[i];
        float base_ground_y = screen_h_ - 8.0f - 8.0f;
        float item_y = base_ground_y - item.current_size;
        if (cursor_x >= item.base_x && cursor_x <= (item.base_x + item.current_size) &&
            cursor_y >= (item_y - 30.0f) && cursor_y <= (base_ground_y + 12.0f)) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void DesktopShell::render_top_bar(float /*elapsed_time*/) {
    glUseProgram(shell_program_);
    glUniform2f(u_screen_size_, static_cast<float>(screen_w_), static_cast<float>(screen_h_));

    // 1. Tło paska menu (28 px wysokości, półprzezroczyste matowe szkło macOS)
    glUniform4f(u_rect_, 0.0f, 0.0f, static_cast<float>(screen_w_), TOP_BAR_HEIGHT);
    glUniform4f(u_color_, 0.96f, 0.96f, 0.97f, 0.82f);
    glUniform4f(u_border_color_, 0.0f, 0.0f, 0.0f, 0.12f);
    glUniform1f(u_radius_, 0.0f);
    glUniform1i(u_type_, 0);

    if (blur_tex_ != 0) {
        glUniform1i(u_has_blur_, 1);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, blur_tex_);
        glUniform1i(u_blur_tex_, 0);
    } else {
        glUniform1i(u_has_blur_, 0);
    }

    glBindVertexArray(vao_);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindTexture(GL_TEXTURE_2D, 0);

    // 2. Autorskie logo systemu Aqua (precyzyjna ikona wektorowa z przezroczystością)
    if (status_icons_) {
        status_icons_->draw_icon(StatusIconType::AquaLogo, 14.0f, 6.5f, 15.0f, 15.0f, 0x1A1A1CFF);
    }

    // 3. Typografia lewej strony: aktywna aplikacja i pozycje menu
    if (font_) {
        // Pogrubiona nazwa aktywnej aplikacji
        font_->draw_text(active_app_name_.c_str(), 36.0f, 6.0f, 0x1A1A1CFF);

        // Standardowe pozycje menu macOS z równymi odstępami
        const char* menu_items[] = {"File", "Edit", "View", "Window", "Help"};
        float cur_menu_x = 36.0f + font_->measure_text_width(active_app_name_) + 18.0f;
        for (const char* item : menu_items) {
            font_->draw_text(item, cur_menu_x, 6.0f, 0x2C2C2EFF);
            cur_menu_x += font_->measure_text_width(item) + 16.0f;
        }
    }

    // 4. Prawa strona: Ikony statusu (Wi-Fi, Bateria, Spotlight, Control Centre) i Zegar z diagramu Apple
    std::time_t t = std::time(nullptr);
    std::tm* now = std::localtime(&t);
    char time_str[64];
    std::strftime(time_str, sizeof(time_str), "%a %d %b  %H:%M", now);

    float right_margin = 16.0f;
    float time_w = font_ ? font_->measure_text_width(time_str) : 90.0f;
    float time_x = screen_w_ - right_margin - time_w;

    if (font_) {
        font_->draw_text(time_str, time_x, 6.0f, 0x1A1A1CFF);
    }

    // Odsuwamy się w lewo od zegara z zachowaniem 18 px oddechu
    float cur_icon_x = time_x - 18.0f;

    if (status_icons_) {
        // A. Control Centre (suwaki)
        cur_icon_x -= 16.0f;
        status_icons_->draw_icon(StatusIconType::ControlCenter, cur_icon_x, 7.5f, 16.0f, 13.0f, 0x2C2C2EFF);

        // B. Spotlight (lupa)
        cur_icon_x -= (14.0f + 16.0f);
        status_icons_->draw_icon(StatusIconType::Spotlight, cur_icon_x, 7.0f, 14.0f, 14.0f, 0x2C2C2EFF);

        // C. Bateria (panoramiczna 24x11 px z diagramu Apple)
        cur_icon_x -= (24.0f + 16.0f);
        status_icons_->draw_icon(StatusIconType::Battery, cur_icon_x, 8.5f, 24.0f, 11.0f, 0x2C2C2EFF);

        // D. Wi-Fi (16x12 px)
        cur_icon_x -= (16.0f + 16.0f);
        status_icons_->draw_icon(StatusIconType::Wifi, cur_icon_x, 8.0f, 16.0f, 12.0f, 0x2C2C2EFF);
    }
}

void DesktopShell::render_dock(float cursor_x, float cursor_y) {
    glUseProgram(shell_program_);
    glUniform2f(u_screen_size_, static_cast<float>(screen_w_), static_cast<float>(screen_h_));

    const float base_icon_size = 48.0f;
    const float max_icon_size = 86.0f;
    const float padding_x = 12.0f;
    const float padding_y = 8.0f;
    const float spacing = 10.0f;
    const float bottom_margin = 8.0f;
    const float separator_gap = 8.0f;
    const float separator_width = 1.0f;

    const float dock_fixed_h = base_icon_size + padding_y * 2.0f;
    const float dock_y = screen_h_ - dock_fixed_h - bottom_margin;

    bool is_over_dock = (cursor_y >= (dock_y - (max_icon_size - base_icon_size) - 20.0f)) &&
                        (cursor_y <= screen_h_);

    const float wave_radius = base_icon_size * 2.2f;
    const float PI = 3.1415926535f;

    int hovered_idx = -1;
    float max_factor = 0.0f;

    // 1. Obliczanie rozmiarów ikon
    for (size_t i = 0; i < dock_items_.size(); ++i) {
        auto& item = dock_items_[i];
        float size = base_icon_size;

        if (is_over_dock) {
            float icon_center_x = item.base_x + item.current_size * 0.5f;
            float dist_x = std::abs(cursor_x - icon_center_x);

            if (dist_x < wave_radius) {
                float factor = 0.5f * (1.0f + std::cos((PI * dist_x) / wave_radius));
                size += (max_icon_size - base_icon_size) * factor;

                if (factor > max_factor) {
                    max_factor = factor;
                    hovered_idx = static_cast<int>(i);
                }
            }
        }
        item.current_size = size;
    }

    // 2. Całkowita szerokość Docka (z uwzględnieniem separatora przed Koszem)
    float total_icons_w = 0.0f;
    for (size_t i = 0; i < dock_items_.size(); ++i) {
        total_icons_w += dock_items_[i].current_size;
        if (i + 1 < dock_items_.size()) {
            if (i == dock_items_.size() - 2) {
                total_icons_w += separator_gap * 2.0f + separator_width;
            } else {
                total_icons_w += spacing;
            }
        }
    }

    float dock_w = total_icons_w + padding_x * 2.0f;
    float dock_x = (screen_w_ - dock_w) * 0.5f;

    // 3. RYSOWANIE SZKLANEJ KAPSUŁY DOCKA (Stała wysokość, zaokrąglenie 18 px)
    glUniform4f(u_rect_, dock_x, dock_y, dock_w, dock_fixed_h);
    glUniform4f(u_color_, 0.95f, 0.95f, 0.97f, 0.62f);
    glUniform4f(u_border_color_, 1.0f, 1.0f, 1.0f, 0.85f);
    glUniform1f(u_radius_, 18.0f);
    glUniform1i(u_type_, 1);

    if (blur_tex_ != 0) {
        glUniform1i(u_has_blur_, 1);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, blur_tex_);
        glUniform1i(u_blur_tex_, 0);
    } else {
        glUniform1i(u_has_blur_, 0);
    }

    glBindVertexArray(vao_);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindTexture(GL_TEXTURE_2D, 0);

    glUniform1i(u_has_blur_, 0);

    // 4. RYSOWANIE IKON WYRÓWNANYCH DO DOLNEJ LINII DOCKA Z FIZYKĄ ODBICIA (BOUNCE)
    float base_ground_y = screen_h_ - bottom_margin - padding_y;

    float cur_x = dock_x + padding_x;
    float tooltip_x = 0.0f;
    float tooltip_y = 0.0f;
    std::string tooltip_text = "";

    // A. Tła squircli oraz separator
    for (size_t i = 0; i < dock_items_.size(); ++i) {
        auto& item = dock_items_[i];
        item.base_x = cur_x;

        float bounce_offset = item.is_bouncing ? compute_bounce_offset(item.bounce_timer) : 0.0f;
        float item_y = base_ground_y - item.current_size - bounce_offset;

        float r = ((item.color >> 16) & 0xFF) / 255.0f;
        float g = ((item.color >> 8) & 0xFF) / 255.0f;
        float b = (item.color & 0xFF) / 255.0f;

        glUniform4f(u_rect_, cur_x, item_y, item.current_size, item.current_size);
        glUniform4f(u_color_, r, g, b, 1.0f);
        glUniform4f(u_border_color_, 1.0f, 1.0f, 1.0f, 0.40f);
        glUniform1f(u_radius_, item.current_size * 0.225f);
        glUniform1i(u_type_, 2);
        glDrawArrays(GL_TRIANGLES, 0, 6);

        if (static_cast<int>(i) == hovered_idx && is_over_dock && max_factor > 0.4f) {
            tooltip_x = cur_x + item.current_size * 0.5f;
            tooltip_y = item_y - 28.0f;
            tooltip_text = item.name;
        }

        if (i == dock_items_.size() - 2) {
            float sep_x = cur_x + item.current_size + separator_gap;
            float sep_h = base_icon_size * 0.70f;
            float sep_y = dock_y + (dock_fixed_h - sep_h) * 0.5f;

            glUniform4f(u_rect_, sep_x, sep_y, separator_width, sep_h);
            glUniform4f(u_color_, 1.0f, 1.0f, 1.0f, 0.45f);
            glUniform1f(u_radius_, 0.5f);
            glUniform1i(u_type_, 3);
            glDrawArrays(GL_TRIANGLES, 0, 6);

            cur_x += item.current_size + separator_gap * 2.0f + separator_width;
        } else {
            cur_x += item.current_size + spacing;
        }
    }

    // B. Wektorowe symbole wewnątrz ikon (StatusIconRenderer)
    if (status_icons_) {
        for (size_t i = 0; i < dock_items_.size(); ++i) {
            const auto& item = dock_items_[i];

            float bounce_offset = item.is_bouncing ? compute_bounce_offset(item.bounce_timer) : 0.0f;
            float item_y = base_ground_y - item.current_size - bounce_offset;
            float glyph_size = item.current_size * 0.56f;
            float glyph_x = item.base_x + (item.current_size - glyph_size) * 0.5f;
            float glyph_y = item_y + (item.current_size - glyph_size) * 0.5f;

            uint32_t tint = 0xFFFFFFFF;
            if (item.icon_type == StatusIconType::Terminal) tint = 0x30D158FF; // Neonowy terminal green
            else if (item.icon_type == StatusIconType::Sysmon) tint = 0x30D158FF; // Neonowy puls EKG
            else if (item.icon_type == StatusIconType::Trash) tint = 0x3A3A3CFF; // Ciemny kontur kosza

            status_icons_->draw_icon(item.icon_type, glyph_x, glyph_y, glyph_size, glyph_size, tint);
        }
    }

    // C. Kropki aktywnych aplikacji na kickboardzie Docka
    glUseProgram(shell_program_);
    glBindVertexArray(vao_);
    for (size_t i = 0; i < dock_items_.size(); ++i) {
        const auto& item = dock_items_[i];
        if (item.is_running) {
            float dot_size = 4.0f;
            float dot_x = item.base_x + (item.current_size - dot_size) * 0.5f;
            float dot_y = base_ground_y + 2.5f;
            glUniform4f(u_rect_, dot_x, dot_y, dot_size, dot_size);
            glUniform4f(u_color_, 0.15f, 0.15f, 0.18f, 0.85f);
            glUniform1f(u_radius_, 2.0f);
            glUniform1i(u_type_, 3);
            glDrawArrays(GL_TRIANGLES, 0, 6);
        }
    }

    // D. Tooltip nad powiększoną ikoną
    if (!tooltip_text.empty() && font_) {
        float text_w = font_->measure_text_width(tooltip_text);
        float badge_w = text_w + 16.0f;
        float badge_h = 20.0f;
        float badge_x = tooltip_x - badge_w * 0.5f;

        glUseProgram(shell_program_);
        glUniform4f(u_rect_, badge_x, tooltip_y, badge_w, badge_h);
        glUniform4f(u_color_, 0.15f, 0.15f, 0.18f, 0.88f);
        glUniform4f(u_border_color_, 1.0f, 1.0f, 1.0f, 0.25f);
        glUniform1f(u_radius_, 6.0f);
        glUniform1i(u_type_, 4);
        glDrawArrays(GL_TRIANGLES, 0, 6);

        font_->draw_text(tooltip_text, badge_x + 8.0f, tooltip_y + 3.0f, 0xFFFFFFFF);
    }
}

void DesktopShell::render(float cursor_x, float cursor_y, float elapsed_time, GLuint blur_texture) {
    cursor_x_ = cursor_x;
    cursor_y_ = cursor_y;
    blur_tex_ = blur_texture;

    float dt = 0.016f;
    if (last_elapsed_ > 0.0f && elapsed_time > last_elapsed_) {
        dt = elapsed_time - last_elapsed_;
        if (dt > 0.1f) dt = 0.1f;
    }
    last_elapsed_ = elapsed_time;

    const float MAX_BOUNCE_TIME = 5.0f;

    for (auto& item : dock_items_) {
        if (item.is_bouncing) {
            float prev_timer = item.bounce_timer;
            item.bounce_timer += dt;

            int prev_cycle = static_cast<int>(prev_timer / DOCK_BOUNCE_PERIOD);
            int cur_cycle = static_cast<int>(item.bounce_timer / DOCK_BOUNCE_PERIOD);

            // Kończymy podskoki dokładnie przy dotknięciu podłoża:
            // 1. Aplikacja się uruchomiła (is_running) i wykonała co najmniej 1 pełny skok
            // 2. Lub upłynął limit 5 sekund (np. dla Kosza lub nieuruchomionych apek)
            bool should_stop = (item.bounce_timer >= MAX_BOUNCE_TIME) ||
                               (item.is_running && item.bounce_timer >= DOCK_BOUNCE_PERIOD);

            if (should_stop && (cur_cycle > prev_cycle || item.bounce_timer >= (MAX_BOUNCE_TIME + DOCK_BOUNCE_PERIOD))) {
                item.is_bouncing = false;
                item.bounce_timer = 0.0f;
            }
        }
    }

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // 1. Rysujemy Top Menu Bar
    render_top_bar(elapsed_time);

    // 2. Rysujemy Dock w wiernym stylu macOS 15 Sequoia
    render_dock(cursor_x, cursor_y);

    glDisable(GL_BLEND);
}

void DesktopShell::shutdown() {
    if (status_icons_) { status_icons_->shutdown(); status_icons_.reset(); }
    if (font_) { font_->shutdown(); font_.reset(); }
    if (vao_) { glDeleteVertexArrays(1, &vao_); vao_ = 0; }
    if (vbo_) { glDeleteBuffers(1, &vbo_); vbo_ = 0; }
    if (shell_program_) { glDeleteProgram(shell_program_); shell_program_ = 0; }
}

} // namespace aqua
