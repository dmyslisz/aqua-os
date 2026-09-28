#include "compositor.hpp"
#include <iostream>
#include <algorithm>

namespace aqua {

namespace {

GLuint compile_shader(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    return s;
}

} // namespace

WindowCompositor::WindowCompositor() = default;

WindowCompositor::~WindowCompositor() {
    shutdown();
}

bool WindowCompositor::initialize(uint32_t screen_width, uint32_t screen_height) {
    screen_w_ = screen_width;
    screen_h_ = screen_height;

    // Shader okna macOS: Signed Distance Field (SDF) dla zaokrąglonych rogów, cienia i przycisków
    const char* vs_src = R"(#version 300 es
        layout (location = 0) in vec2 aPos;
        uniform vec2 u_screen_size;
        uniform vec4 u_rect; // x, y, width, height (w tym obszar cienia)

        out vec2 v_local_pos; // piksele w układzie okna

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

        uniform vec4 u_rect; // x, y, width, height całego wyrenderowanego quada
        uniform vec4 u_win_box; // x_offset, y_offset, win_w, win_h
        uniform float u_radius;
        uniform vec2 u_cursor_pos;
        uniform int u_pass; // 0 = Shadow, 1 = Window body & Traffic Lights
        uniform sampler2D u_client_tex;
        uniform int u_has_client_tex;
        uniform int u_full_size_content;

        // Funkcja Signed Distance Field (SDF) dla zaokrąglonego prostokąta
        float sdRoundedBox(vec2 p, vec2 b, float r) {
            vec2 q = abs(p) - b + vec2(r);
            return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r;
        }

        void main() {
            vec2 win_center = u_win_box.xy + u_win_box.zw * 0.5;
            vec2 p = v_local_pos - win_center;
            vec2 half_size = u_win_box.zw * 0.5;

            float d = sdRoundedBox(p, half_size, u_radius);

            if (u_pass == 0) {
                // RENDEROWANIE MIĘKKIEGO CIENIA (macOS Drop Shadow)
                vec2 shadow_p = v_local_pos - (win_center + vec2(0.0, 8.0));
                float sd_shadow = sdRoundedBox(shadow_p, half_size, u_radius);
                
                float shadow_alpha = 1.0 - smoothstep(-5.0, 26.0, sd_shadow);
                shadow_alpha = pow(shadow_alpha, 1.8) * 0.42;

                if (shadow_alpha <= 0.005) discard;
                FragColor = vec4(0.0, 0.0, 0.0, shadow_alpha);
                return;
            }

            // RENDEROWANIE KORPUSU OKNA
            if (d > 0.0) discard;

            float local_y = v_local_pos.y - u_win_box.y;
            float local_x = v_local_pos.x - u_win_box.x;

            // 1. Tło korpusu okna lub treść aplikacji (z dma-buf)
            vec3 bg_color = vec3(0.96, 0.96, 0.97);

            if (u_has_client_tex == 1) {
                vec2 uv = vec2(local_x / u_win_box.z, local_y / u_win_box.w);
                vec4 tex_col = texture(u_client_tex, uv);
                bg_color = tex_col.rgb;
            } else {
                // Domyślny pasek tytułowy gdy brak klienta
                if (local_y < 38.0) {
                    bg_color = mix(vec3(0.93, 0.93, 0.94), vec3(0.89, 0.89, 0.90), local_y / 38.0);
                }
                if (abs(local_y - 38.0) < 0.6) {
                    bg_color = vec3(0.80, 0.80, 0.82);
                }
            }

            // 4. KONTROLKI TRAFFIC LIGHTS (Lewy górny róg)
            // Przyciski mają promień 6 px (średnica 12 px)
            float btn_cy = 19.0;
            float r_btn = 6.0;

            vec3 btn_color = bg_color;
            bool is_button = false;

            // Pozycja kursora względem lewego górnego rogu okna
            float cur_local_x = u_cursor_pos.x - (u_rect.x + u_win_box.x);
            float cur_local_y = u_cursor_pos.y - (u_rect.y + u_win_box.y);

            // Czerwony (Zamknij) - środek: (18, 19)
            float d_red = length(vec2(local_x - 18.0, local_y - btn_cy)) - r_btn;
            bool hover_red = length(vec2(cur_local_x - 18.0, cur_local_y - btn_cy)) <= (r_btn + 1.0);
            if (d_red < 0.0) {
                btn_color = vec3(1.0, 0.37, 0.34); // #FF5F56
                is_button = true;
                // Symbol 'X' pojawia się TYLKO gdy kursor jest bezpośrednio nad czerwonym przyciskiem
                if (hover_red) {
                    vec2 dxy = abs(vec2(local_x - 18.0, local_y - btn_cy));
                    if (abs(dxy.x - dxy.y) < 0.75 && dxy.x < 3.2) {
                        btn_color = vec3(0.35, 0.0, 0.0);
                    }
                }
            }

            // Żółty (Minimalizuj) - środek: (38, 19)
            float d_yellow = length(vec2(local_x - 38.0, local_y - btn_cy)) - r_btn;
            bool hover_yellow = length(vec2(cur_local_x - 38.0, cur_local_y - btn_cy)) <= (r_btn + 1.0);
            if (d_yellow < 0.0) {
                btn_color = vec3(1.0, 0.74, 0.18); // #FFBD2E
                is_button = true;
                // Symbol '-' pojawia się TYLKO gdy kursor jest nad żółtym przyciskiem
                if (hover_yellow) {
                    if (abs(local_y - btn_cy) < 0.85 && abs(local_x - 38.0) < 3.5) {
                        btn_color = vec3(0.42, 0.22, 0.0);
                    }
                }
            }

            // Zielony (Maksymalizuj) - środek: (58, 19)
            float d_green = length(vec2(local_x - 58.0, local_y - btn_cy)) - r_btn;
            bool hover_green = length(vec2(cur_local_x - 58.0, cur_local_y - btn_cy)) <= (r_btn + 1.0);
            if (d_green < 0.0) {
                btn_color = vec3(0.15, 0.79, 0.25); // #27C93F
                is_button = true;
                // Symbol '+' pojawia się TYLKO gdy kursor jest nad zielonym przyciskiem
                if (hover_green) {
                    float gx = local_x - 58.0;
                    float gy = local_y - btn_cy;
                    if ((abs(gx) < 0.75 && abs(gy) < 3.2) || (abs(gy) < 0.75 && abs(gx) < 3.2)) {
                        btn_color = vec3(0.0, 0.35, 0.05);
                    }
                }
            }

            vec3 final_color = is_button ? btn_color : bg_color;

            // 5. Delikatna obwódka okna (Border 1 px w stylu macOS)
            if (d > -1.0) {
                final_color = mix(final_color, vec3(0.0, 0.0, 0.0), 0.18);
            }

            // Antialiasing krawędzi okna
            float edge_alpha = clamp(-d, 0.0, 1.0);
            FragColor = vec4(final_color, edge_alpha);
        }
    )";

    GLuint vs = compile_shader(GL_VERTEX_SHADER, vs_src);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, fs_src);
    program_ = glCreateProgram();
    glAttachShader(program_, vs);
    glAttachShader(program_, fs);
    glLinkProgram(program_);
    glDeleteShader(vs);
    glDeleteShader(fs);

    u_screen_size_ = glGetUniformLocation(program_, "u_screen_size");
    u_rect_ = glGetUniformLocation(program_, "u_rect");
    u_radius_ = glGetUniformLocation(program_, "u_radius");
    u_cursor_pos_ = glGetUniformLocation(program_, "u_cursor_pos");
    u_win_box_ = glGetUniformLocation(program_, "u_win_box");
    u_pass_ = glGetUniformLocation(program_, "u_pass");
    u_client_tex_ = glGetUniformLocation(program_, "u_client_tex");
    u_has_client_tex_ = glGetUniformLocation(program_, "u_has_client_tex");

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

void WindowCompositor::update_screen_size(uint32_t screen_width, uint32_t screen_height) {
    screen_w_ = screen_width;
    screen_h_ = screen_height;
}

void WindowCompositor::add_window(std::shared_ptr<Window> win) {
    windows_.push_back(std::move(win));
}

void WindowCompositor::remove_window(uint32_t id) {
    windows_.erase(
        std::remove_if(windows_.begin(), windows_.end(),
                       [id](const auto& w) { return w->id() == id; }),
        windows_.end()
    );
}

void WindowCompositor::handle_pointer_move(float cursor_x, float cursor_y) {
    cursor_x_ = cursor_x;
    cursor_y_ = cursor_y;

    if (dragging_window_ && is_button_down_) {
        // Płynne przesuwanie okna
        dragging_window_->set_position(cursor_x - drag_offset_x_, cursor_y - drag_offset_y_);
    } else if (resizing_window_ && is_button_down_) {
        // Płynna zmiana rozmiaru okna (Window Resizing)
        float dx = cursor_x - resize_start_x_;
        float dy = cursor_y - resize_start_y_;

        float new_x = resize_orig_x_;
        float new_y = resize_orig_y_;
        float new_w = resize_orig_w_;
        float new_h = resize_orig_h_;

        const float min_w = 300.0f;
        const float min_h = 180.0f;

        switch (resizing_edge_) {
            case WindowEdge::Right:
                new_w = std::max(min_w, resize_orig_w_ + dx);
                break;
            case WindowEdge::Bottom:
                new_h = std::max(min_h, resize_orig_h_ + dy);
                break;
            case WindowEdge::BottomRight:
                new_w = std::max(min_w, resize_orig_w_ + dx);
                new_h = std::max(min_h, resize_orig_h_ + dy);
                break;
            case WindowEdge::Left:
                if (resize_orig_w_ - dx >= min_w) {
                    new_x = resize_orig_x_ + dx;
                    new_w = resize_orig_w_ - dx;
                }
                break;
            case WindowEdge::Top:
                if (resize_orig_h_ - dy >= min_h) {
                    new_y = resize_orig_y_ + dy;
                    new_h = resize_orig_h_ - dy;
                }
                break;
            case WindowEdge::BottomLeft:
                if (resize_orig_w_ - dx >= min_w) {
                    new_x = resize_orig_x_ + dx;
                    new_w = resize_orig_w_ - dx;
                }
                new_h = std::max(min_h, resize_orig_h_ + dy);
                break;
            case WindowEdge::TopRight:
                if (resize_orig_h_ - dy >= min_h) {
                    new_y = resize_orig_y_ + dy;
                    new_h = resize_orig_h_ - dy;
                }
                new_w = std::max(min_w, resize_orig_w_ + dx);
                break;
            case WindowEdge::TopLeft:
                if (resize_orig_w_ - dx >= min_w) {
                    new_x = resize_orig_x_ + dx;
                    new_w = resize_orig_w_ - dx;
                }
                if (resize_orig_h_ - dy >= min_h) {
                    new_y = resize_orig_y_ + dy;
                    new_h = resize_orig_h_ - dy;
                }
                break;
            default:
                break;
        }

        resizing_window_->set_position(new_x, new_y);
        resizing_window_->set_size(new_w, new_h);
    }
}

void WindowCompositor::handle_pointer_button(uint32_t button, bool pressed, float cursor_x, float cursor_y) {
    // 272 = BTN_LEFT w linuksie/evdev
    if (button == 272) {
        is_button_down_ = pressed;

        if (pressed) {
            // Szukamy okna od góry (najwyższy Z-order)
            for (auto it = windows_.rbegin(); it != windows_.rend(); ++it) {
                auto win = *it;

                // 1. Sprawdź kliknięcie w Traffic Lights
                auto tl = win->hit_test_traffic_lights(cursor_x, cursor_y);
                if (tl == TrafficLightButton::Close) {
                    std::cout << "[Aqua] Kliknieto zamkniecie okna ID: " << win->id() << std::endl;
                    remove_window(win->id());
                    return;
                } else if (tl == TrafficLightButton::Minimize) {
                    std::cout << "[Aqua] Kliknieto minimalizacje okna ID: " << win->id() << std::endl;
                    return;
                } else if (tl == TrafficLightButton::Maximize) {
                    std::cout << "[Aqua] Kliknieto maksymalizacje okna ID: " << win->id() << std::endl;
                    return;
                }

                // 2. Sprawdź chwycenie za krawędź do zmiany rozmiaru (Resizing)
                auto edge = win->hit_test_edge(cursor_x, cursor_y);
                if (edge != WindowEdge::None) {
                    resizing_window_ = win;
                    resizing_edge_ = edge;
                    resize_start_x_ = cursor_x;
                    resize_start_y_ = cursor_y;
                    resize_orig_x_ = win->x();
                    resize_orig_y_ = win->y();
                    resize_orig_w_ = win->width();
                    resize_orig_h_ = win->height();

                    // Przenieś kliknięte okno na sam wierzch
                    windows_.erase(std::next(it).base());
                    windows_.push_back(win);
                    return;
                }

                // 3. Sprawdź chwycenie za pasek tytułowy (Draggable Region)
                if (win->is_in_draggable_region(cursor_x, cursor_y)) {
                    dragging_window_ = win;
                    drag_offset_x_ = cursor_x - win->x();
                    drag_offset_y_ = cursor_y - win->y();

                    // Przenieś kliknięte okno na sam wierzch
                    windows_.erase(std::next(it).base());
                    windows_.push_back(win);
                    return;
                }

                // 4. Jeśli kliknięto wewnątrz okna, przenieś na wierzch (Focus / Bring to front)
                if (win->contains(cursor_x, cursor_y)) {
                    windows_.erase(std::next(it).base());
                    windows_.push_back(win);
                    return;
                }
            }
        } else {
            // Zwolnienie przycisku myszy
            dragging_window_ = nullptr;
            resizing_window_ = nullptr;
        }
    }
}

void WindowCompositor::render_window(const Window& win, float cursor_x, float cursor_y) {
    float shadow_margin = 35.0f; // Margines na rozmyty cień

    float quad_x = win.x() - shadow_margin;
    float quad_y = win.y() - shadow_margin;
    float quad_w = win.width() + shadow_margin * 2.0f;
    float quad_h = win.height() + shadow_margin * 2.0f;

    glUseProgram(program_);
    glUniform2f(u_screen_size_, static_cast<float>(screen_w_), static_cast<float>(screen_h_));
    glUniform4f(u_rect_, quad_x, quad_y, quad_w, quad_h);
    glUniform1f(u_radius_, Window::CORNER_RADIUS);
    glUniform2f(u_cursor_pos_, cursor_x, cursor_y);
    glUniform4f(u_win_box_, shadow_margin, shadow_margin, win.width(), win.height());

    glBindVertexArray(vao_);

    // PRZEBIEG 1: RENDEROWANIE MIĘKKIEGO CIENIA
    glUniform1i(u_pass_, 0);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    // PRZEBIEG 2: RENDEROWANIE KORPUSU OKNA (LUB TREŚCI KLIENTA) I TRAFFIC LIGHTS
    glUniform1i(u_pass_, 1);
    if (win.has_texture()) {
        glUniform1i(u_has_client_tex_, 1);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, win.texture_id());
        glUniform1i(u_client_tex_, 0);
    } else {
        glUniform1i(u_has_client_tex_, 0);
    }

    glDrawArrays(GL_TRIANGLES, 0, 6);

    if (win.has_texture()) {
        glBindTexture(GL_TEXTURE_2D, 0);
    }
}

void WindowCompositor::render() {
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    for (const auto& win : windows_) {
        render_window(*win, cursor_x_, cursor_y_);
    }

    glDisable(GL_BLEND);
}

void WindowCompositor::shutdown() {
    if (vao_) { glDeleteVertexArrays(1, &vao_); vao_ = 0; }
    if (vbo_) { glDeleteBuffers(1, &vbo_); vbo_ = 0; }
    if (program_) { glDeleteProgram(program_); program_ = 0; }
    windows_.clear();
}

} // namespace aqua
