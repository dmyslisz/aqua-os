#pragma once

#include "color.hpp"
#include <string>
#include <memory>
#include <unordered_map>
#include <vector>
#include <ft2build.h>
#include FT_FREETYPE_H

namespace aqua {

struct Glyph {
    int width{0};
    int rows{0};
    int left{0};
    int top{0};
    int advance_x{0};
    std::vector<uint8_t> buffer;
};

class Font {
public:
    Font();
    ~Font();

    bool load_file(const std::string& path, uint32_t pixel_size);
    bool load_system_default(uint32_t pixel_size = 14, bool bold = false);

    const Glyph* get_glyph(char32_t cp);
    int measure_text_width(const std::string& text);
    int pixel_size() const { return pixel_size_; }
    int line_height() const { return line_height_; }
    int ascender() const { return ascender_; }

    static std::shared_ptr<Font> get_default(uint32_t size = 14);
    static std::shared_ptr<Font> get_bold(uint32_t size = 14);

private:
    FT_Library ft_{nullptr};
    FT_Face face_{nullptr};
    uint32_t pixel_size_{14};
    int ascender_{11};
    int line_height_{18};
    std::unordered_map<char32_t, Glyph> glyph_cache_;
};

} // namespace aqua
