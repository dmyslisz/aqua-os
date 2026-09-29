#pragma once

#include "window.hpp"
#include "aqua_protocol.h"
#include <vector>
#include <memory>
#include <functional>
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

    using WindowResizeCallback = std::function<void(uint32_t window_id, uint32_t width, uint32_t height)>;
    using WindowInputCallback = std::function<void(uint32_t window_id, MessageType type, const MsgInputEvent& event)>;
    using WindowCloseCallback = std::function<void(uint32_t window_id)>;

    void set_resize_callback(WindowResizeCallback cb) { resize_cb_ = std::move(cb); }
    void set_input_callback(WindowInputCallback cb) { input_cb_ = std::move(cb); }
    void set_close_callback(WindowCloseCallback cb) { close_cb_ = std::move(cb); }
    uint32_t focused_window_id() const;

    // Renderowanie wszystkich okien, cieni i kontrolek
    void render();

    void shutdown();

    size_t window_count() const { return windows_.size(); }
    const std::vector<std::shared_ptr<Window>>& windows() const { return windows_; }

    std::shared_ptr<Window> find_window_by_title(const std::string& query) const {
        for (auto it = windows_.rbegin(); it != windows_.rend(); ++it) {
            if ((*it)->title().find(query) != std::string::npos) {
                return *it;
            }
        }
        return nullptr;
    }

    bool bring_to_front(uint32_t id) {
        for (auto it = windows_.begin(); it != windows_.end(); ++it) {
            if ((*it)->id() == id) {
                auto win = *it;
                windows_.erase(it);
                windows_.push_back(win);
                return true;
            }
        }
        return false;
    }

private:
    void render_window(const Window& win, float cursor_x, float cursor_y);

    uint32_t screen_w_{1920};
    uint32_t screen_h_{1080};

    std::vector<std::shared_ptr<Window>> windows_;
    std::shared_ptr<Window> dragging_window_{nullptr};
    float drag_offset_x_{0.0f};
    float drag_offset_y_{0.0f};

    std::shared_ptr<Window> resizing_window_{nullptr};
    WindowEdge resizing_edge_{WindowEdge::None};
    float resize_start_x_{0.0f};
    float resize_start_y_{0.0f};
    float resize_orig_x_{0.0f};
    float resize_orig_y_{0.0f};
    float resize_orig_w_{0.0f};
    float resize_orig_h_{0.0f};

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
    GLint u_cursor_pos_{-1};
    GLint u_win_box_{-1};
    GLint u_pass_{-1};
    GLint u_client_tex_{-1};
    GLint u_has_client_tex_{-1};

    WindowResizeCallback resize_cb_;
    WindowInputCallback input_cb_;
    WindowCloseCallback close_cb_;
};

} // namespace aqua
