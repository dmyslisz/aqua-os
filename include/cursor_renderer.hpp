#pragma once

#include <cstdint>
#include <GLES3/gl3.h>

namespace aqua {

class CursorRenderer {
public:
    CursorRenderer();
    ~CursorRenderer();

    bool initialize(uint32_t screen_width, uint32_t screen_height);
    void update_screen_size(uint32_t screen_width, uint32_t screen_height);

    void set_position(float x, float y);
    void move(float dx, float dy);

    float x() const { return x_; }
    float y() const { return y_; }

    void render();
    void shutdown();

private:
    void generate_macos_cursor_texture();

    uint32_t screen_w_{1920};
    uint32_t screen_h_{1080};
    float x_{960.0f};
    float y_{540.0f};

    GLuint program_{0};
    GLuint vao_{0};
    GLuint vbo_{0};
    GLuint texture_{0};

    GLint pos_loc_{-1};
    GLint screen_size_loc_{-1};
    GLint cursor_size_loc_{-1};

    static constexpr int CURSOR_TEX_SIZE = 32;
};

} // namespace aqua
