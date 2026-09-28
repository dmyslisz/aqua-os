#pragma once

#include <cstdint>
#include <GLES3/gl3.h>

namespace aqua {

enum class StatusIconType {
    AquaLogo,
    Battery,
    Wifi,
    Spotlight,
    ControlCenter
};

class StatusIconRenderer {
public:
    StatusIconRenderer();
    ~StatusIconRenderer();

    bool initialize(uint32_t screen_width, uint32_t screen_height);
    void update_screen_size(uint32_t screen_width, uint32_t screen_height);

    void draw_icon(StatusIconType type, float x, float y, float size_w, float size_h, uint32_t tint_rgba = 0x1A1A1CFF);
    void shutdown();

private:
    void generate_icon_textures();

    uint32_t screen_w_{1920};
    uint32_t screen_h_{1080};

    GLuint program_{0};
    GLuint vao_{0};
    GLuint vbo_{0};

    GLuint tex_logo_{0};
    GLuint tex_battery_{0};
    GLuint tex_wifi_{0};
    GLuint tex_spotlight_{0};
    GLuint tex_control_center_{0};

    GLint u_screen_size_{-1};
    GLint u_rect_{-1};
    GLint u_color_{-1};
    GLint u_tex_{-1};

    static constexpr int ICON_RES = 32;
};

} // namespace aqua
