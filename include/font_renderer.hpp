#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <GLES3/gl3.h>

namespace aqua {

class FontRenderer {
public:
    FontRenderer();
    ~FontRenderer();

    bool initialize(uint32_t screen_width, uint32_t screen_height);
    void update_screen_size(uint32_t screen_width, uint32_t screen_height);

    void draw_text(const std::string& text, float x, float y, float scale, uint32_t color_rgba);
    void shutdown();

private:
    void generate_font_atlas();

    uint32_t screen_w_{1920};
    uint32_t screen_h_{1080};

    GLuint program_{0};
    GLuint vao_{0};
    GLuint vbo_{0};
    GLuint font_texture_{0};

    GLint u_screen_size_{-1};
    GLint u_rect_{-1};
    GLint u_uv_rect_{-1};
    GLint u_color_{-1};
};

} // namespace aqua
