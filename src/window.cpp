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

WindowEdge Window::hit_test_edge(float cursor_x, float cursor_y) const {
    const float border = 8.0f; // Strefa uchwytu zmiany rozmiaru

    // Sprawdź czy kursor jest w obrębie obrysu okna (z tolerancją border)
    if (cursor_x < (x_ - border) || cursor_x > (x_ + width_ + border) ||
        cursor_y < (y_ - border) || cursor_y > (y_ + height_ + border)) {
        return WindowEdge::None;
    }

    bool on_left = cursor_x <= (x_ + border);
    bool on_right = cursor_x >= (x_ + width_ - border);
    bool on_top = cursor_y <= (y_ + border);
    bool on_bottom = cursor_y >= (y_ + height_ - border);

    if (on_top && on_left) return WindowEdge::TopLeft;
    if (on_top && on_right) return WindowEdge::TopRight;
    if (on_bottom && on_left) return WindowEdge::BottomLeft;
    if (on_bottom && on_right) return WindowEdge::BottomRight;

    if (on_left) return WindowEdge::Left;
    if (on_right) return WindowEdge::Right;
    if (on_top) return WindowEdge::Top;
    if (on_bottom) return WindowEdge::Bottom;

    return WindowEdge::None;
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
