#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace aqua {

enum class TrafficLightButton {
    None,
    Close,
    Minimize,
    Maximize
};

struct Rect {
    float x{0.0f};
    float y{0.0f};
    float width{0.0f};
    float height{0.0f};

    bool contains(float px, float py) const {
        return px >= x && px <= (x + width) && py >= y && py <= (y + height);
    }
};

class Window {
public:
    Window(uint32_t id, float x, float y, float width, float height, const std::string& title);

    uint32_t id() const { return id_; }
    const std::string& title() const { return title_; }

    float x() const { return x_; }
    float y() const { return y_; }
    float width() const { return width_; }
    float height() const { return height_; }

    void set_position(float x, float y) { x_ = x; y_ = y; }
    void set_size(float w, float h) { width_ = w; height_ = h; }

    bool is_focused() const { return is_focused_; }
    void set_focused(bool f) { is_focused_ = f; }

    bool full_size_content() const { return full_size_content_; }
    void set_full_size_content(bool f) { full_size_content_ = f; }

    // Hit-testing dla Traffic Lights
    TrafficLightButton hit_test_traffic_lights(float cursor_x, float cursor_y) const;

    // Hit-testing dla strefy przeciągania (Draggable Titlebar Region)
    bool is_in_draggable_region(float cursor_x, float cursor_y) const;

    // Obsługa zewnętrznej tekstury z dma-buf
    uint32_t texture_id() const { return texture_id_; }
    void set_texture(uint32_t tex_id) { texture_id_ = tex_id; }
    bool has_texture() const { return texture_id_ != 0; }

    // Metryki kontrolek macOS Traffic Lights
    static constexpr float TRAFFIC_LIGHTS_WIDTH = 72.0f;
    static constexpr float TITLEBAR_HEIGHT = 38.0f;
    static constexpr float CORNER_RADIUS = 12.0f;
    static constexpr float BUTTON_RADIUS = 6.0f;

private:
    uint32_t id_{0};
    float x_{100.0f};
    float y_{100.0f};
    float width_{800.0f};
    float height_{520.0f};
    std::string title_;

    uint32_t texture_id_{0};
    bool is_focused_{true};
    bool full_size_content_{true};
};

} // namespace aqua
