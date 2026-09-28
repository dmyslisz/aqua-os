#include "kawase_blur.hpp"
#include <iostream>
#include <vector>

namespace aqua {

namespace {

GLuint compile_shader(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);

    GLint success = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &success);
    if (!success) {
        char log[512];
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        std::cerr << "[Kawase Shader BŁĄD] " << log << std::endl;
    }
    return s;
}

GLuint link_program(GLuint vs, GLuint fs) {
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    return prog;
}

} // namespace

KawaseBlur::KawaseBlur() = default;

KawaseBlur::~KawaseBlur() {
    shutdown();
}

bool KawaseBlur::initialize(uint32_t screen_width, uint32_t screen_height) {
    screen_w_ = screen_width;
    screen_h_ = screen_height;

    // Wspólny vertex shader dla pełnoekranowego quada
    const char* vs_src = R"(#version 300 es
        layout (location = 0) in vec2 aPos;
        out vec2 v_uv;
        void main() {
            v_uv = aPos * 0.5 + 0.5;
            gl_Position = vec4(aPos, 0.0, 1.0);
        }
    )";

    // Fragment shader downsample (Kawase 5-tap)
    const char* fs_down_src = R"(#version 300 es
        precision highp float;
        in vec2 v_uv;
        out vec4 FragColor;

        uniform sampler2D u_tex;
        uniform vec2 u_halfpixel;
        uniform float u_offset;

        void main() {
            vec4 sum = texture(u_tex, v_uv) * 4.0;
            sum += texture(u_tex, v_uv - u_halfpixel * u_offset);
            sum += texture(u_tex, v_uv + u_halfpixel * u_offset);
            sum += texture(u_tex, v_uv + vec2(u_halfpixel.x, -u_halfpixel.y) * u_offset);
            sum += texture(u_tex, v_uv + vec2(-u_halfpixel.x, u_halfpixel.y) * u_offset);
            FragColor = sum / 8.0;
        }
    )";

    // Fragment shader upsample (Kawase 8-tap)
    const char* fs_up_src = R"(#version 300 es
        precision highp float;
        in vec2 v_uv;
        out vec4 FragColor;

        uniform sampler2D u_tex;
        uniform vec2 u_halfpixel;
        uniform float u_offset;

        void main() {
            vec4 sum = vec4(0.0);
            sum += texture(u_tex, v_uv + vec2(-u_halfpixel.x * 2.0, 0.0) * u_offset);
            sum += texture(u_tex, v_uv + vec2(-u_halfpixel.x, u_halfpixel.y) * u_offset) * 2.0;
            sum += texture(u_tex, v_uv + vec2(0.0, u_halfpixel.y * 2.0) * u_offset);
            sum += texture(u_tex, v_uv + vec2(u_halfpixel.x, u_halfpixel.y) * u_offset) * 2.0;
            sum += texture(u_tex, v_uv + vec2(u_halfpixel.x * 2.0, 0.0) * u_offset);
            sum += texture(u_tex, v_uv + vec2(u_halfpixel.x, -u_halfpixel.y) * u_offset) * 2.0;
            sum += texture(u_tex, v_uv + vec2(0.0, -u_halfpixel.y * 2.0) * u_offset);
            sum += texture(u_tex, v_uv + vec2(-u_halfpixel.x, -u_halfpixel.y) * u_offset) * 2.0;
            FragColor = sum / 12.0;
        }
    )";

    // Fragment shader blit (scena -> ekran)
    const char* fs_blit_src = R"(#version 300 es
        precision highp float;
        in vec2 v_uv;
        out vec4 FragColor;
        uniform sampler2D u_tex;

        void main() {
            FragColor = texture(u_tex, v_uv);
        }
    )";

    GLuint vs = compile_shader(GL_VERTEX_SHADER, vs_src);
    GLuint fs_down = compile_shader(GL_FRAGMENT_SHADER, fs_down_src);
    GLuint fs_up = compile_shader(GL_FRAGMENT_SHADER, fs_up_src);
    GLuint fs_blit = compile_shader(GL_FRAGMENT_SHADER, fs_blit_src);

    downsample_prog_ = link_program(vs, fs_down);
    upsample_prog_ = link_program(vs, fs_up);
    blit_prog_ = link_program(vs, fs_blit);

    glDeleteShader(vs);
    glDeleteShader(fs_down);
    glDeleteShader(fs_up);
    glDeleteShader(fs_blit);

    u_down_tex_ = glGetUniformLocation(downsample_prog_, "u_tex");
    u_down_halfpixel_ = glGetUniformLocation(downsample_prog_, "u_halfpixel");
    u_down_offset_ = glGetUniformLocation(downsample_prog_, "u_offset");

    u_up_tex_ = glGetUniformLocation(upsample_prog_, "u_tex");
    u_up_halfpixel_ = glGetUniformLocation(upsample_prog_, "u_halfpixel");
    u_up_offset_ = glGetUniformLocation(upsample_prog_, "u_offset");

    u_blit_tex_ = glGetUniformLocation(blit_prog_, "u_tex");

    // Pełnoekranowy quad (-1..1)
    float quad_vertices[] = {
        -1.0f, -1.0f,
         1.0f, -1.0f,
        -1.0f,  1.0f,
        -1.0f,  1.0f,
         1.0f, -1.0f,
         1.0f,  1.0f
    };

    glGenVertexArrays(1, &quad_vao_);
    glGenBuffers(1, &quad_vbo_);

    glBindVertexArray(quad_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, quad_vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad_vertices), quad_vertices, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), nullptr);
    glEnableVertexAttribArray(0);

    init_fbos();
    std::cout << "[Aqua Blur] Zainicjalizowano potok Dual-Kawase Frosted Glass dla " 
              << screen_w_ << "x" << screen_h_ << std::endl;
    return true;
}

void KawaseBlur::init_fbos() {
    // 1. Główny FBO sceny (pełny ekran)
    glGenFramebuffers(1, &scene_fbo_);
    glGenTextures(1, &scene_tex_);

    glBindFramebuffer(GL_FRAMEBUFFER, scene_fbo_);
    glBindTexture(GL_TEXTURE_2D, scene_tex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, screen_w_, screen_h_, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, scene_tex_, 0);

    // 2. FBO piramidy rozmycia
    uint32_t w = screen_w_;
    uint32_t h = screen_h_;

    for (int i = 0; i < NUM_LEVELS; ++i) {
        w = std::max(1u, w / 2);
        h = std::max(1u, h / 2);

        // Downsample
        glGenFramebuffers(1, &downsample_fbo_[i]);
        glGenTextures(1, &downsample_tex_[i]);
        glBindFramebuffer(GL_FRAMEBUFFER, downsample_fbo_[i]);
        glBindTexture(GL_TEXTURE_2D, downsample_tex_[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, downsample_tex_[i], 0);

        // Upsample
        glGenFramebuffers(1, &upsample_fbo_[i]);
        glGenTextures(1, &upsample_tex_[i]);
        glBindFramebuffer(GL_FRAMEBUFFER, upsample_fbo_[i]);
        glBindTexture(GL_TEXTURE_2D, upsample_tex_[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, upsample_tex_[i], 0);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void KawaseBlur::cleanup_fbos() {
    if (scene_fbo_) { glDeleteFramebuffers(1, &scene_fbo_); scene_fbo_ = 0; }
    if (scene_tex_) { glDeleteTextures(1, &scene_tex_); scene_tex_ = 0; }

    for (int i = 0; i < NUM_LEVELS; ++i) {
        if (downsample_fbo_[i]) { glDeleteFramebuffers(1, &downsample_fbo_[i]); downsample_fbo_[i] = 0; }
        if (downsample_tex_[i]) { glDeleteTextures(1, &downsample_tex_[i]); downsample_tex_[i] = 0; }
        if (upsample_fbo_[i]) { glDeleteFramebuffers(1, &upsample_fbo_[i]); upsample_fbo_[i] = 0; }
        if (upsample_tex_[i]) { glDeleteTextures(1, &upsample_tex_[i]); upsample_tex_[i] = 0; }
    }
}

void KawaseBlur::update_screen_size(uint32_t screen_width, uint32_t screen_height) {
    if (screen_w_ == screen_width && screen_h_ == screen_height) return;
    screen_w_ = screen_width;
    screen_h_ = screen_height;
    cleanup_fbos();
    init_fbos();
}

void KawaseBlur::begin_scene() {
    glBindFramebuffer(GL_FRAMEBUFFER, scene_fbo_);
    glViewport(0, 0, screen_w_, screen_h_);
}

void KawaseBlur::end_scene() {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void KawaseBlur::process_blur() {
    glDisable(GL_BLEND);
    glBindVertexArray(quad_vao_);

    // 1. PRZEBIEGI DOWNSAMPLE
    // Poziom 0: ze scene_tex_ (1920x1080) -> downsample_fbo_[0] (960x540)
    // Poziom 1: z downsample_tex_[0] -> downsample_fbo_[1] (480x270)
    // Poziom 2: z downsample_tex_[1] -> downsample_fbo_[2] (240x135)
    glUseProgram(downsample_prog_);
    glUniform1i(u_down_tex_, 0);
    glActiveTexture(GL_TEXTURE0);

    uint32_t cur_w = screen_w_;
    uint32_t cur_h = screen_h_;

    for (int i = 0; i < NUM_LEVELS; ++i) {
        uint32_t next_w = std::max(1u, cur_w / 2);
        uint32_t next_h = std::max(1u, cur_h / 2);

        glBindFramebuffer(GL_FRAMEBUFFER, downsample_fbo_[i]);
        glViewport(0, 0, next_w, next_h);

        GLuint input_tex = (i == 0) ? scene_tex_ : downsample_tex_[i - 1];
        glBindTexture(GL_TEXTURE_2D, input_tex);

        glUniform2f(u_down_halfpixel_, 0.5f / cur_w, 0.5f / cur_h);
        glUniform1f(u_down_offset_, 2.0f);

        glDrawArrays(GL_TRIANGLES, 0, 6);

        cur_w = next_w;
        cur_h = next_h;
    }

    // 2. PRZEBIEGI UPSAMPLE
    // Poziom 2 -> Poziom 1 (240x135 -> 480x270)
    // Poziom 1 -> Poziom 0 (480x270 -> 960x540)
    glUseProgram(upsample_prog_);
    glUniform1i(u_up_tex_, 0);

    for (int i = NUM_LEVELS - 1; i >= 0; --i) {
        uint32_t target_w = screen_w_;
        uint32_t target_h = screen_h_;
        for (int k = 0; k <= i; ++k) {
            target_w = std::max(1u, target_w / 2);
            target_h = std::max(1u, target_h / 2);
        }

        glBindFramebuffer(GL_FRAMEBUFFER, upsample_fbo_[i]);
        glViewport(0, 0, target_w, target_h);

        GLuint input_tex;
        uint32_t src_w, src_h;
        if (i == NUM_LEVELS - 1) {
            input_tex = downsample_tex_[i];
            src_w = target_w;
            src_h = target_h;
        } else {
            input_tex = upsample_tex_[i + 1];
            src_w = target_w / 2;
            src_h = target_h / 2;
        }

        glBindTexture(GL_TEXTURE_2D, input_tex);
        glUniform2f(u_up_halfpixel_, 0.5f / src_w, 0.5f / src_h);
        glUniform1f(u_up_offset_, 2.5f);

        glDrawArrays(GL_TRIANGLES, 0, 6);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void KawaseBlur::draw_scene_to_screen() {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, screen_w_, screen_h_);
    glDisable(GL_BLEND);

    glUseProgram(blit_prog_);
    glUniform1i(u_blit_tex_, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, scene_tex_);

    glBindVertexArray(quad_vao_);
    glDrawArrays(GL_TRIANGLES, 0, 6);

    glBindTexture(GL_TEXTURE_2D, 0);
}

void KawaseBlur::shutdown() {
    cleanup_fbos();
    if (quad_vao_) { glDeleteVertexArrays(1, &quad_vao_); quad_vao_ = 0; }
    if (quad_vbo_) { glDeleteBuffers(1, &quad_vbo_); quad_vbo_ = 0; }
    if (downsample_prog_) { glDeleteProgram(downsample_prog_); downsample_prog_ = 0; }
    if (upsample_prog_) { glDeleteProgram(upsample_prog_); upsample_prog_ = 0; }
    if (blit_prog_) { glDeleteProgram(blit_prog_); blit_prog_ = 0; }
}

} // namespace aqua
