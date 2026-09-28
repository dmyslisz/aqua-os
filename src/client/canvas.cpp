#include "aqua/canvas.hpp"
#include "aqua/font.hpp"
#include "aqua/font_8x16.hpp"
#include <cstring>
#include <cmath>

namespace aqua {

void Canvas::clear(Color color) {
    if (!buffer_ || width_ == 0 || height_ == 0) return;

    uint32_t c32 = color.to_u32();
    for (uint32_t y = 0; y < height_; ++y) {
        uint32_t* row = reinterpret_cast<uint32_t*>(buffer_ + y * stride_);
        for (uint32_t x = 0; x < width_; ++x) {
            row[x] = c32;
        }
    }
}

void Canvas::set_pixel(int x, int y, Color color) {
    if (x < 0 || x >= static_cast<int>(width_) || y < 0 || y >= static_cast<int>(height_)) return;
    uint32_t* p = reinterpret_cast<uint32_t*>(buffer_ + y * stride_ + x * 4);
    *p = color.to_u32();
}

Color Canvas::get_pixel(int x, int y) const {
    if (x < 0 || x >= static_cast<int>(width_) || y < 0 || y >= static_cast<int>(height_)) {
        return Color::Clear;
    }
    const uint8_t* p = buffer_ + y * stride_ + x * 4;
    return Color(p[0], p[1], p[2], p[3]);
}

void Canvas::blend_pixel(int x, int y, Color color) {
    if (x < 0 || x >= static_cast<int>(width_) || y < 0 || y >= static_cast<int>(height_)) return;
    if (color.a == 255) {
        set_pixel(x, y, color);
        return;
    }
    if (color.a == 0) return;

    Color cur = get_pixel(x, y);
    set_pixel(x, y, color.blend_over(cur));
}

void Canvas::fill_rect(int x, int y, int w, int h, Color color) {
    if (!buffer_ || w <= 0 || h <= 0) return;

    int x0 = std::clamp(x, 0, static_cast<int>(width_));
    int y0 = std::clamp(y, 0, static_cast<int>(height_));
    int x1 = std::clamp(x + w, 0, static_cast<int>(width_));
    int y1 = std::clamp(y + h, 0, static_cast<int>(height_));

    if (x0 >= x1 || y0 >= y1) return;

    if (color.a == 255) {
        uint32_t c32 = color.to_u32();
        for (int py = y0; py < y1; ++py) {
            uint32_t* row = reinterpret_cast<uint32_t*>(buffer_ + py * stride_);
            for (int px = x0; px < x1; ++px) {
                row[px] = c32;
            }
        }
    } else {
        for (int py = y0; py < y1; ++py) {
            for (int px = x0; px < x1; ++px) {
                blend_pixel(px, py, color);
            }
        }
    }
}

void Canvas::draw_rect(int x, int y, int w, int h, Color color, int thickness) {
    if (w <= 0 || h <= 0 || thickness <= 0) return;
    fill_rect(x, y, w, thickness, color);
    fill_rect(x, y + h - thickness, w, thickness, color);
    fill_rect(x, y + thickness, thickness, h - 2 * thickness, color);
    fill_rect(x + w - thickness, y + thickness, thickness, h - 2 * thickness, color);
}

void Canvas::fill_rounded_rect(int x, int y, int w, int h, int radius, Color color) {
    if (w <= 0 || h <= 0) return;
    radius = std::clamp(radius, 0, std::min(w, h) / 2);
    if (radius <= 0) {
        fill_rect(x, y, w, h, color);
        return;
    }

    // Środkowe prostokąty (krzyż)
    fill_rect(x + radius, y, w - 2 * radius, h, color);
    fill_rect(x, y + radius, radius, h - 2 * radius, color);
    fill_rect(x + w - radius, y + radius, radius, h - 2 * radius, color);

    // Cztery zaokrąglone narożniki
    int r2 = radius * radius;
    for (int dy = 0; dy < radius; ++dy) {
        for (int dx = 0; dx < radius; ++dx) {
            int d = (radius - 1 - dx) * (radius - 1 - dx) + (radius - 1 - dy) * (radius - 1 - dy);
            if (d <= r2) {
                // Lewy górny
                blend_pixel(x + dx, y + dy, color);
                // Prawy górny
                blend_pixel(x + w - 1 - dx, y + dy, color);
                // Lewy dolny
                blend_pixel(x + dx, y + h - 1 - dy, color);
                // Prawy dolny
                blend_pixel(x + w - 1 - dx, y + h - 1 - dy, color);
            }
        }
    }
}

void Canvas::draw_rounded_rect(int x, int y, int w, int h, int radius, Color color, int thickness) {
    // Prosta implementacja obrysu
    for (int t = 0; t < thickness; ++t) {
        // Poziome i pionowe krawędzie
        int cur_x = x + t;
        int cur_y = y + t;
        int cur_w = w - 2 * t;
        int cur_h = h - 2 * t;
        int cur_r = std::max(0, radius - t);

        if (cur_w <= 0 || cur_h <= 0) break;

        // Krawędzie poziome
        fill_rect(cur_x + cur_r, cur_y, cur_w - 2 * cur_r, 1, color);
        fill_rect(cur_x + cur_r, cur_y + cur_h - 1, cur_w - 2 * cur_r, 1, color);
        // Krawędzie pionowe
        fill_rect(cur_x, cur_y + cur_r, 1, cur_h - 2 * cur_r, color);
        fill_rect(cur_x + cur_w - 1, cur_y + cur_r, 1, cur_h - 2 * cur_r, color);

        if (cur_r > 0) {
            int r2 = cur_r * cur_r;
            int inner_r2 = (cur_r - 1) * (cur_r - 1);
            for (int dy = 0; dy < cur_r; ++dy) {
                for (int dx = 0; dx < cur_r; ++dx) {
                    int d = (cur_r - 1 - dx) * (cur_r - 1 - dx) + (cur_r - 1 - dy) * (cur_r - 1 - dy);
                    if (d <= r2 && d >= inner_r2) {
                        blend_pixel(cur_x + dx, cur_y + dy, color);
                        blend_pixel(cur_x + cur_w - 1 - dx, cur_y + dy, color);
                        blend_pixel(cur_x + dx, cur_y + cur_h - 1 - dy, color);
                        blend_pixel(cur_x + cur_w - 1 - dx, cur_y + cur_h - 1 - dy, color);
                    }
                }
            }
        }
    }
}

void Canvas::fill_circle(int cx, int cy, int radius, Color color) {
    if (radius <= 0) return;
    int r2 = radius * radius;
    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
            if (dx * dx + dy * dy <= r2) {
                blend_pixel(cx + dx, cy + dy, color);
            }
        }
    }
}

void Canvas::draw_circle(int cx, int cy, int radius, Color color, int thickness) {
    if (radius <= 0 || thickness <= 0) return;
    int outer_r2 = radius * radius;
    int inner_r = std::max(0, radius - thickness);
    int inner_r2 = inner_r * inner_r;

    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
            int d2 = dx * dx + dy * dy;
            if (d2 <= outer_r2 && d2 >= inner_r2) {
                blend_pixel(cx + dx, cy + dy, color);
            }
        }
    }
}

void Canvas::draw_line(int x0, int y0, int x1, int y1, Color color, int thickness) {
    int dx = std::abs(x1 - x0);
    int dy = std::abs(y1 - y0);
    int sx = (x0 < x1) ? 1 : -1;
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx - dy;

    int half_t = thickness / 2;

    while (true) {
        if (thickness <= 1) {
            blend_pixel(x0, y0, color);
        } else {
            fill_rect(x0 - half_t, y0 - half_t, thickness, thickness, color);
        }

        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x0 += sx;
        }
        if (e2 < dx) {
            err += dx;
            y0 += sy;
        }
    }
}

void Canvas::draw_gradient_v(int x, int y, int w, int h, Color top, Color bottom) {
    if (w <= 0 || h <= 0) return;
    for (int py = 0; py < h; ++py) {
        float factor = static_cast<float>(py) / (h - 1 > 0 ? h - 1 : 1);
        uint8_t r = static_cast<uint8_t>(top.r + (bottom.r - top.r) * factor);
        uint8_t g = static_cast<uint8_t>(top.g + (bottom.g - top.g) * factor);
        uint8_t b = static_cast<uint8_t>(top.b + (bottom.b - top.b) * factor);
        uint8_t a = static_cast<uint8_t>(top.a + (bottom.a - top.a) * factor);
        fill_rect(x, y + py, w, 1, Color(r, g, b, a));
    }
}

void Canvas::draw_gradient_h(int x, int y, int w, int h, Color left, Color right) {
    if (w <= 0 || h <= 0) return;
    for (int px = 0; px < w; ++px) {
        float factor = static_cast<float>(px) / (w - 1 > 0 ? w - 1 : 1);
        uint8_t r = static_cast<uint8_t>(left.r + (right.r - left.r) * factor);
        uint8_t g = static_cast<uint8_t>(left.g + (right.g - left.g) * factor);
        uint8_t b = static_cast<uint8_t>(left.b + (right.b - left.b) * factor);
        uint8_t a = static_cast<uint8_t>(left.a + (right.a - left.a) * factor);
        fill_rect(x + px, y, 1, h, Color(r, g, b, a));
    }
}

void Canvas::draw_char(int x, int y, char c, Color color, int scale) {
    uint8_t uc = static_cast<uint8_t>(c);
    const uint8_t* glyph = FONT_8X16[uc];

    for (int row = 0; row < 16; ++row) {
        uint8_t line = glyph[row];
        if (line == 0) continue;

        for (int col = 0; col < 8; ++col) {
            if (line & (0x80 >> col)) {
                if (scale <= 1) {
                    blend_pixel(x + col, y + row, color);
                } else {
                    fill_rect(x + col * scale, y + row * scale, scale, scale, color);
                }
            }
        }
    }
}

void Canvas::draw_text_bitmap(int x, int y, const std::string& text, Color color, int scale) {
    int cur_x = x;
    int char_w = 8 * scale;
    for (char c : text) {
        if (c == '\n') {
            cur_x = x;
            y += 16 * scale + 2;
            continue;
        }
        draw_char(cur_x, y, c, color, scale);
        cur_x += char_w;
    }
}

void Canvas::draw_text_bitmap_right(int right_x, int y, const std::string& text, Color color, int scale) {
    int total_w = measure_bitmap_width(text, scale);
    draw_text_bitmap(right_x - total_w, y, text, color, scale);
}

void Canvas::draw_text_bitmap_centered(int cx, int cy, const std::string& text, Color color, int scale) {
    int total_w = measure_bitmap_width(text, scale);
    int total_h = 16 * scale;
    draw_text_bitmap(cx - total_w / 2, cy - total_h / 2, text, color, scale);
}

int Canvas::measure_bitmap_width(const std::string& text, int scale) const {
    size_t len = 0;
    size_t max_len = 0;
    for (char c : text) {
        if (c == '\n') {
            max_len = std::max(max_len, len);
            len = 0;
        } else {
            len++;
        }
    }
    max_len = std::max(max_len, len);
    return static_cast<int>(max_len * 8 * scale);
}

void Canvas::draw_text(int x, int y, const std::string& text, Color color, uint32_t font_size, bool bold) {
    if (!buffer_ || width_ == 0 || height_ == 0) return;
    if (font_size == 0) font_size = 14;

    auto font = bold ? Font::get_bold(font_size) : Font::get_default(font_size);
    if (!font) {
        // Fallback do czcionki rastrowej
        draw_text_bitmap(x, y, text, color, std::max(1, static_cast<int>(font_size) / 16));
        return;
    }

    int cur_x = x;
    int baseline_y = y + font->ascender();

    for (size_t i = 0; i < text.size(); ++i) {
        char c = text[i];
        if (c == '\n') {
            cur_x = x;
            baseline_y += font->line_height();
            continue;
        }

        const Glyph* g = font->get_glyph(static_cast<char32_t>(static_cast<unsigned char>(c)));
        if (!g) continue;

        int gx0 = cur_x + g->left;
        int gy0 = baseline_y - g->top;

        for (int r = 0; r < g->rows; ++r) {
            int py = gy0 + r;
            if (py < 0 || py >= static_cast<int>(height_)) continue;

            for (int col = 0; col < g->width; ++col) {
                int px = gx0 + col;
                if (px < 0 || px >= static_cast<int>(width_)) continue;

                uint8_t cov = g->buffer[r * g->width + col];
                if (cov > 0) {
                    uint8_t a = static_cast<uint8_t>((color.a * cov) / 255);
                    blend_pixel(px, py, Color(color.r, color.g, color.b, a));
                }
            }
        }

        cur_x += g->advance_x;
    }
}

void Canvas::draw_text_right(int right_x, int y, const std::string& text, Color color, uint32_t font_size, bool bold) {
    int w = measure_text_width(text, font_size, bold);
    draw_text(right_x - w, y, text, color, font_size, bold);
}

void Canvas::draw_text_centered(int cx, int cy, const std::string& text, Color color, uint32_t font_size, bool bold) {
    int w = measure_text_width(text, font_size, bold);
    draw_text(cx - w / 2, cy - static_cast<int>(font_size) / 2, text, color, font_size, bold);
}

int Canvas::measure_text_width(const std::string& text, uint32_t font_size, bool bold) {
    auto font = bold ? Font::get_bold(font_size) : Font::get_default(font_size);
    if (font) {
        return font->measure_text_width(text);
    }
    return measure_bitmap_width(text, std::max(1, static_cast<int>(font_size) / 16));
}

} // namespace aqua
