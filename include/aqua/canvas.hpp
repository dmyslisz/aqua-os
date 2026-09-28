#pragma once

#include "color.hpp"
#include <cstdint>
#include <string>
#include <algorithm>

namespace aqua {

class Canvas {
public:
    Canvas(uint8_t* buffer, uint32_t width, uint32_t height, uint32_t stride)
        : buffer_(buffer), width_(width), height_(height), stride_(stride) {}

    uint32_t width() const { return width_; }
    uint32_t height() const { return height_; }
    uint32_t stride() const { return stride_; }
    uint8_t* buffer() const { return buffer_; }

    void clear(Color color);
    void set_pixel(int x, int y, Color color);
    void blend_pixel(int x, int y, Color color);
    Color get_pixel(int x, int y) const;

    void fill_rect(int x, int y, int w, int h, Color color);
    void draw_rect(int x, int y, int w, int h, Color color, int thickness = 1);

    void fill_rounded_rect(int x, int y, int w, int h, int radius, Color color);
    void draw_rounded_rect(int x, int y, int w, int h, int radius, Color color, int thickness = 1);

    void fill_circle(int cx, int cy, int radius, Color color);
    void draw_circle(int cx, int cy, int radius, Color color, int thickness = 1);

    void draw_line(int x0, int y0, int x1, int y1, Color color, int thickness = 1);

    void draw_gradient_v(int x, int y, int w, int h, Color top, Color bottom);
    void draw_gradient_h(int x, int y, int w, int h, Color left, Color right);

    // Renderowanie tekstu z wbudowaną czcionką rastrową 8x16
    void draw_char(int x, int y, char c, Color color, int scale = 1);
    void draw_text(int x, int y, const std::string& text, Color color, int scale = 1);
    void draw_text_right(int right_x, int y, const std::string& text, Color color, int scale = 1);
    void draw_text_centered(int cx, int cy, const std::string& text, Color color, int scale = 1);
    int measure_text_width(const std::string& text, int scale = 1) const;
    int font_height(int scale = 1) const;

private:
    uint8_t* buffer_{nullptr};
    uint32_t width_{0};
    uint32_t height_{0};
    uint32_t stride_{0};
};

} // namespace aqua
