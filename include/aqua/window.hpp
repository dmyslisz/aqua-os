#pragma once

#include "canvas.hpp"
#include "color.hpp"
#include <string>
#include <memory>

namespace aqua {

class Application;

class AppWindow {
public:
    AppWindow(const std::string& title, uint32_t width, uint32_t height);
    virtual ~AppWindow();

    uint32_t id() const { return window_id_; }
    uint32_t width() const { return width_; }
    uint32_t height() const { return height_; }
    const std::string& title() const { return title_; }

    void request_redraw() { needs_redraw_ = true; }
    bool needs_redraw() const { return needs_redraw_; }

    virtual void on_create() {}
    virtual void on_draw(Canvas& canvas) = 0;
    virtual void on_resize(uint32_t new_width, uint32_t new_height) { (void)new_width; (void)new_height; }
    virtual void on_mouse_move(float x, float y) { (void)x; (void)y; }
    virtual void on_mouse_down(float x, float y, uint32_t button) { (void)x; (void)y; (void)button; }
    virtual void on_mouse_up(float x, float y, uint32_t button) { (void)x; (void)y; (void)button; }
    virtual void on_key_down(uint32_t key_code) { (void)key_code; }
    virtual void on_key_up(uint32_t key_code) { (void)key_code; }
    virtual void on_close() {}

    bool should_close() const { return should_close_; }
    void close() { should_close_ = true; }

private:
    friend class Application;

    bool init_shm(int sock, uint32_t win_id);
    void handle_resize(int sock, uint32_t new_w, uint32_t new_h);
    void render_and_commit(int sock);
    void cleanup();

    std::string title_;
    uint32_t width_{640};
    uint32_t height_{400};
    uint32_t stride_{0};
    uint32_t window_id_{0};

    int shm_fd_{-1};
    uint8_t* pixels_{nullptr};
    size_t shm_capacity_{0};

    bool needs_redraw_{true};
    bool should_close_{false};
};

} // namespace aqua
