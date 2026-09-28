#include "aqua/font.hpp"
#include <iostream>
#include <vector>
#include <algorithm>

namespace aqua {

namespace {

const std::vector<std::string> REGULAR_CANDIDATES = {
    "/usr/local/share/fonts/dejavu/DejaVuSans.ttf",
    "/usr/local/share/fonts/TTF/DejaVuSans.ttf",
    "/usr/local/share/fonts/cantarell/Cantarell-Regular.otf",
    "/usr/local/share/fonts/Liberation/LiberationSans-Regular.ttf",
    "/usr/local/share/fonts/freefont-ttf/FreeSans.ttf",
    "/usr/local/share/fonts/noto/NotoSans-Regular.ttf"
};

const std::vector<std::string> BOLD_CANDIDATES = {
    "/usr/local/share/fonts/dejavu/DejaVuSans-Bold.ttf",
    "/usr/local/share/fonts/TTF/DejaVuSans-Bold.ttf",
    "/usr/local/share/fonts/cantarell/Cantarell-Bold.otf",
    "/usr/local/share/fonts/Liberation/LiberationSans-Bold.ttf",
    "/usr/local/share/fonts/freefont-ttf/FreeSansBold.ttf",
    "/usr/local/share/fonts/noto/NotoSans-Bold.ttf"
};

static std::unordered_map<uint32_t, std::shared_ptr<Font>> s_regular_cache;
static std::unordered_map<uint32_t, std::shared_ptr<Font>> s_bold_cache;

} // namespace

Font::Font() {
    if (FT_Init_FreeType(&ft_)) {
        ft_ = nullptr;
    }
}

Font::~Font() {
    if (face_) {
        FT_Done_Face(face_);
        face_ = nullptr;
    }
    if (ft_) {
        FT_Done_FreeType(ft_);
        ft_ = nullptr;
    }
}

bool Font::load_file(const std::string& path, uint32_t pixel_size) {
    if (!ft_) return false;

    if (face_) {
        FT_Done_Face(face_);
        face_ = nullptr;
    }

    if (FT_New_Face(ft_, path.c_str(), 0, &face_)) {
        return false;
    }

    FT_Set_Pixel_Sizes(face_, 0, pixel_size);
    pixel_size_ = pixel_size;

    ascender_ = static_cast<int>(face_->size->metrics.ascender >> 6);
    line_height_ = static_cast<int>(face_->size->metrics.height >> 6);

    if (ascender_ <= 0) ascender_ = static_cast<int>(pixel_size * 0.8f);
    if (line_height_ <= 0) line_height_ = static_cast<int>(pixel_size * 1.3f);

    glyph_cache_.clear();

    // Wstępnie renderujemy znaki ASCII dla błyskawicznego rysowania 60 FPS
    for (char32_t c = 32; c <= 126; ++c) {
        get_glyph(c);
    }

    return true;
}

bool Font::load_system_default(uint32_t pixel_size, bool bold) {
    const auto& candidates = bold ? BOLD_CANDIDATES : REGULAR_CANDIDATES;
    for (const auto& path : candidates) {
        if (load_file(path, pixel_size)) {
            return true;
        }
    }
    // Jeśli odmiana pogrubiona nie została znaleziona, spróbuj regularnej
    if (bold) {
        for (const auto& path : REGULAR_CANDIDATES) {
            if (load_file(path, pixel_size)) {
                return true;
            }
        }
    }
    return false;
}

const Glyph* Font::get_glyph(char32_t cp) {
    auto it = glyph_cache_.find(cp);
    if (it != glyph_cache_.end()) {
        return &it->second;
    }

    if (!face_) return nullptr;

    if (FT_Load_Char(face_, cp, FT_LOAD_RENDER)) {
        return nullptr;
    }

    Glyph g;
    g.width = face_->glyph->bitmap.width;
    g.rows = face_->glyph->bitmap.rows;
    g.left = face_->glyph->bitmap_left;
    g.top = face_->glyph->bitmap_top;
    g.advance_x = static_cast<int>(face_->glyph->advance.x >> 6);

    int size = g.width * g.rows;
    if (size > 0 && face_->glyph->bitmap.buffer) {
        g.buffer.assign(face_->glyph->bitmap.buffer, face_->glyph->bitmap.buffer + size);
    }

    auto inserted = glyph_cache_.emplace(cp, std::move(g));
    return &inserted.first->second;
}

int Font::measure_text_width(const std::string& text) {
    int max_w = 0;
    int cur_w = 0;

    for (char c : text) {
        if (c == '\n') {
            max_w = std::max(max_w, cur_w);
            cur_w = 0;
            continue;
        }
        const Glyph* g = get_glyph(static_cast<char32_t>(static_cast<unsigned char>(c)));
        if (g) {
            cur_w += g->advance_x;
        } else {
            cur_w += static_cast<int>(pixel_size_ * 0.6f);
        }
    }

    return std::max(max_w, cur_w);
}

std::shared_ptr<Font> Font::get_default(uint32_t size) {
    auto it = s_regular_cache.find(size);
    if (it != s_regular_cache.end()) {
        return it->second;
    }

    auto f = std::make_shared<Font>();
    if (f->load_system_default(size, false)) {
        s_regular_cache[size] = f;
        return f;
    }
    return nullptr;
}

std::shared_ptr<Font> Font::get_bold(uint32_t size) {
    auto it = s_bold_cache.find(size);
    if (it != s_bold_cache.end()) {
        return it->second;
    }

    auto f = std::make_shared<Font>();
    if (f->load_system_default(size, true)) {
        s_bold_cache[size] = f;
        return f;
    }
    return nullptr;
}

} // namespace aqua
