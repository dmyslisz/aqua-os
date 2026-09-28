#include "window.hpp"
#include <cmath>

namespace aqua {

Window::Window(uint32_t id, float x, float y, float width, float height, const std::string& title)
    : id_(id), x_(x), y_(y), width_(width), height_(height), title_(title) {}

bool Window::contains(float cursor_x, float cursor_y) const {
    return cursor_x >= x_ && cursor_x <= (x_ + width_) &&
           cursor_y >= y_ && cursor_y <= (y_ + height_);
}

TrafficLightButton Window::hit_test_traffic_lights(float cursor_x, float cursor_y) const {
    float cy = y_ + 19.0f;
    float r = BUTTON_RADIUS + 3.0f; // Margines tolerancji kliknięcia

    // 1. Czerwony (Zamknij)
    float cx_close = x_ + 18.0f;
    if (std::hypot(cursor_x - cx_close, cursor_y - cy) <= r) {
        return TrafficLightButton::Close;
    }

    // 2. Żółty (Minimalizuj)
    float cx_min = x_ + 38.0f;
    if (std::hypot(cursor_x - cx_min, cursor_y - cy) <= r) {
        return TrafficLightButton::Minimize;
    }

    // 3. Zielony (Maksymalizuj)
    float cx_max = x_ + 58.0f;
    if (std::hypot(cursor_x - cx_max, cursor_y - cy) <= r) {
        return TrafficLightButton::Maximize;
    }

    return TrafficLightButton::None;
}

bool Window::is_in_draggable_region(float cursor_x, float cursor_y) const {
    if (!contains(cursor_x, cursor_y)) return false;

    // Przeciąganie działa w obrębie paska tytułowego (górne 38 px)
    if (cursor_y > (y_ + TITLEBAR_HEIGHT)) return false;

    // Ale wykluczamy strefę Traffic Lights (lewe 72 px)
    if (cursor_x < (x_ + TRAFFIC_LIGHTS_WIDTH)) return false;

    return true;
}

} // namespace aqua
