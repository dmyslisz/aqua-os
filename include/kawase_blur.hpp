#pragma once

#include <cstdint>
#include <GLES3/gl3.h>

namespace aqua {

class KawaseBlur {
public:
    KawaseBlur();
    ~KawaseBlur();

    bool initialize(uint32_t screen_width, uint32_t screen_height);
    void update_screen_size(uint32_t screen_width, uint32_t screen_height);

    void begin_scene(); // Przekieruj renderowanie do scene_fbo_
    void end_scene();   // Zakończ renderowanie sceny

    void process_blur(); // Wykonaj potok Dual-Kawase (downsample + upsample)

    void draw_scene_to_screen(); // Narysuj czysty obraz sceny (tapeta + okna) na ekran

    GLuint scene_fbo() const { return scene_fbo_; }
    GLuint scene_texture() const { return scene_tex_; }
    GLuint blurred_texture() const { return upsample_tex_[0]; } // 960x540 wygładzona tekstura szkła

    void shutdown();

private:
    void init_fbos();
    void cleanup_fbos();

    uint32_t screen_w_{1920};
    uint32_t screen_h_{1080};

    // Scena główna (pełny ekran)
    GLuint scene_fbo_{0};
    GLuint scene_tex_{0};

    // Przebiegi Dual-Kawase
    // Level 0: 1/2 (960x540)
    // Level 1: 1/4 (480x270)
    // Level 2: 1/8 (240x135)
    static constexpr int NUM_LEVELS = 3;
    GLuint downsample_fbo_[NUM_LEVELS]{0};
    GLuint downsample_tex_[NUM_LEVELS]{0};
    GLuint upsample_fbo_[NUM_LEVELS]{0};
    GLuint upsample_tex_[NUM_LEVELS]{0};

    // Shadery
    GLuint downsample_prog_{0};
    GLuint upsample_prog_{0};
    GLuint blit_prog_{0};

    GLuint quad_vao_{0};
    GLuint quad_vbo_{0};

    // Uniformy downsample
    GLint u_down_tex_{-1};
    GLint u_down_halfpixel_{-1};
    GLint u_down_offset_{-1};

    // Uniformy upsample
    GLint u_up_tex_{-1};
    GLint u_up_halfpixel_{-1};
    GLint u_up_offset_{-1};

    // Uniformy blit
    GLint u_blit_tex_{-1};
};

} // namespace aqua
