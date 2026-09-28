#pragma once

#include <cstdint>
#include <string>
#include <map>
#include <memory>
#include <GLES3/gl3.h>
#include <ft2build.h>
#include FT_FREETYPE_H

namespace aqua {

struct Character {
    GLuint texture_id{0};
    int width{0};
    int height{0};
    int bearing_x{0};
    int bearing_y{0};
    uint32_t advance{0};
};

class FontRenderer {
public:
    FontRenderer();
    ~FontRenderer();

    bool initialize(uint32_t screen_width, uint32_t screen_height);
    void update_screen_size(uint32_t screen_width, uint32_t screen_height);

    bool load_font(const std::string& font_path, uint32_t font_size = 14);
    void draw_text(const std::string& text, float x, float y, uint32_t color_rgba = 0x1C1C1EFF);
    float measure_text_width(const std::string& text);

    void shutdown();

private:
    uint32_t screen_w_{1920};
    uint32_t screen_h_{1080};

    FT_Library ft_{nullptr};
    FT_Face face_{nullptr};
    std::map<char, Character> characters_;

    GLuint program_{0};
    GLuint vao_{0};
    GLuint vbo_{0};

    GLint u_screen_size_{-1};
    GLint u_rect_{-1};
    GLint u_color_{-1};
    GLint u_tex_{-1};

    bool is_ready_{false};
};

} // namespace aqua
