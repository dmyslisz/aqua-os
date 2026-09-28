#pragma once

#include "window.hpp"
#include <vector>
#include <memory>
#include <GLES3/gl3.h>

namespace aqua {

class WindowCompositor {
public:
    WindowCompositor();
    ~WindowCompositor();

    bool initialize(uint32_t screen_width, uint32_t screen_height);
    void update_screen_size(uint32_t screen_width, uint32_t screen_height);

    void add_window(std::shared_ptr<Window> win);
    void remove_window(uint32_t id);

    // Obsługa wejścia: ruch myszy, kliknięcia
    void handle_pointer_move(float cursor_x, float cursor_y);
    void handle_pointer_button(uint32_t button, bool pressed, float cursor_x, float cursor_y);

    // Renderowanie wszystkich okien, cieni i kontrolek
    void render();

    void shutdown();

    size_t window_count() const { return windows_.size(); }

private:
    void render_window(const Window& win, float cursor_x, float cursor_y);

    uint32_t screen_w_{1920};
    uint32_t screen_h_{1080};

    std::vector<std::shared_ptr<Window>> windows_;
    std::shared_ptr<Window> dragging_window_{nullptr};
    float drag_offset_x_{0.0f};
    float drag_offset_y_{0.0f};

    float cursor_x_{0.0f};
    float cursor_y_{0.0f};
    bool is_button_down_{false};

    GLuint program_{0};
    GLuint vao_{0};
    GLuint vbo_{0};

    // Lokacje uniformów w shaderze okna
    GLint u_screen_size_{-1};
    GLint u_rect_{-1};
    GLint u_radius_{-1};
    GLint u_color_{-1};
    GLint u_shadow_{-1};
    GLint u_traffic_lights_{-1};
    GLint u_cursor_pos_{-1};
};

} // namespace aqua
