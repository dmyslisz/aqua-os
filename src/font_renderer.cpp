#include "font_renderer.hpp"
#include <iostream>
#include <vector>

namespace aqua {

FontRenderer::FontRenderer() = default;

FontRenderer::~FontRenderer() {
    shutdown();
}

bool FontRenderer::initialize(uint32_t screen_width, uint32_t screen_height) {
    screen_w_ = screen_width;
    screen_h_ = screen_height;

    if (FT_Init_FreeType(&ft_)) {
        std::cerr << "[Aqua Font] Blad inicjalizacji FreeType!" << std::endl;
        return false;
    }

    const char* vs_src = R"(#version 300 es
        layout (location = 0) in vec4 vertex; // x, y, u, v
        uniform vec2 u_screen_size;
        uniform vec4 u_rect;
        out vec2 TexCoords;

        void main() {
            vec2 pixel_pos = u_rect.xy + vertex.xy * u_rect.zw;
            vec2 ndc = (pixel_pos / u_screen_size) * 2.0 - 1.0;
            ndc.y = -ndc.y;
            gl_Position = vec4(ndc, 0.0, 1.0);
            TexCoords = vertex.zw;
        }
    )";

    const char* fs_src = R"(#version 300 es
        precision mediump float;
        in vec2 TexCoords;
        out vec4 FragColor;
        uniform sampler2D u_tex;
        uniform vec4 u_color;

        void main() {
            float alpha = texture(u_tex, TexCoords).r;
            FragColor = vec4(u_color.rgb, u_color.a * alpha);
        }
    )";

    auto compile_s = [](GLenum type, const char* src) -> GLuint {
        GLuint s = glCreateShader(type);
        glShaderSource(s, 1, &src, nullptr);
        glCompileShader(s);
        return s;
    };

    GLuint vs = compile_s(GL_VERTEX_SHADER, vs_src);
    GLuint fs = compile_s(GL_FRAGMENT_SHADER, fs_src);
    program_ = glCreateProgram();
    glAttachShader(program_, vs);
    glAttachShader(program_, fs);
    glLinkProgram(program_);
    glDeleteShader(vs);
    glDeleteShader(fs);

    u_screen_size_ = glGetUniformLocation(program_, "u_screen_size");
    u_rect_ = glGetUniformLocation(program_, "u_rect");
    u_color_ = glGetUniformLocation(program_, "u_color");
    u_tex_ = glGetUniformLocation(program_, "u_tex");

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);

    float quad_vertices[] = {
        0.0f, 0.0f, 0.0f, 0.0f,
        1.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 1.0f,
        0.0f, 1.0f, 0.0f, 1.0f,
        1.0f, 0.0f, 1.0f, 0.0f,
        1.0f, 1.0f, 1.0f, 1.0f
    };
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad_vertices), quad_vertices, GL_STATIC_DRAW);

    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(float), 0);
    glEnableVertexAttribArray(0);

    // Przeszukiwanie standardowych ścieżek fontów we FreeBSD
    std::vector<std::string> font_candidates = {
        "/usr/local/share/fonts/dejavu/DejaVuSans.ttf",
        "/usr/local/share/fonts/dejavu/DejaVuSans-Bold.ttf",
        "/usr/local/share/fonts/TTF/DejaVuSans.ttf",
        "/usr/local/share/fonts/cantarell/Cantarell-Regular.otf",
        "/usr/local/share/fonts/Liberation/LiberationSans-Regular.ttf"
    };

    for (const auto& path : font_candidates) {
        if (load_font(path, 13)) {
            std::cout << "[Aqua Font] Pomyślnie załadowano font wektorowy: " << path << std::endl;
            break;
        }
    }

    return true;
}

bool FontRenderer::load_font(const std::string& font_path, uint32_t font_size) {
    if (face_) {
        FT_Done_Face(face_);
        face_ = nullptr;
    }

    if (FT_New_Face(ft_, font_path.c_str(), 0, &face_)) {
        return false;
    }

    FT_Set_Pixel_Sizes(face_, 0, font_size);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    // Generujemy glify dla znaków ASCII 32..126
    for (unsigned char c = 32; c < 128; c++) {
        if (FT_Load_Char(face_, c, FT_LOAD_RENDER)) {
            continue;
        }

        GLuint tex;
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(
            GL_TEXTURE_2D,
            0,
            GL_R8,
            face_->glyph->bitmap.width,
            face_->glyph->bitmap.rows,
            0,
            GL_RED,
            GL_UNSIGNED_BYTE,
            face_->glyph->bitmap.buffer
        );

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        Character character = {
            tex,
            static_cast<int>(face_->glyph->bitmap.width),
            static_cast<int>(face_->glyph->bitmap.rows),
            face_->glyph->bitmap_left,
            face_->glyph->bitmap_top,
            static_cast<uint32_t>(face_->glyph->advance.x)
        };
        characters_[c] = character;
    }

    is_ready_ = true;
    return true;
}

void FontRenderer::update_screen_size(uint32_t screen_width, uint32_t screen_height) {
    screen_w_ = screen_width;
    screen_h_ = screen_height;
}

void FontRenderer::draw_text(const std::string& text, float x, float y, uint32_t color_rgba) {
    if (!is_ready_) return;

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glUseProgram(program_);
    glUniform2f(u_screen_size_, static_cast<float>(screen_w_), static_cast<float>(screen_h_));

    float r = ((color_rgba >> 24) & 0xFF) / 255.0f;
    float g = ((color_rgba >> 16) & 0xFF) / 255.0f;
    float b = ((color_rgba >> 8) & 0xFF) / 255.0f;
    float a = (color_rgba & 0xFF) / 255.0f;
    glUniform4f(u_color_, r, g, b, a);

    glActiveTexture(GL_TEXTURE0);
    glBindVertexArray(vao_);

    float cur_x = x;
    for (char c : text) {
        auto it = characters_.find(c);
        if (it == characters_.end()) continue;

        const auto& ch = it->second;

        float xpos = cur_x + ch.bearing_x;
        float ypos = y + (12 - ch.bearing_y); // Bazowa linia tekstu (baseline)
        float w = ch.width;
        float h = ch.height;

        if (w > 0 && h > 0) {
            glBindTexture(GL_TEXTURE_2D, ch.texture_id);
            glUniform4f(u_rect_, xpos, ypos, w, h);
            glDrawArrays(GL_TRIANGLES, 0, 6);
        }

        // Advance to 1/64 piksela (bitshift >> 6)
        cur_x += (ch.advance >> 6);
    }

    glDisable(GL_BLEND);
}

float FontRenderer::measure_text_width(const std::string& text) {
    if (!is_ready_) return 0.0f;
    float w = 0.0f;
    for (char c : text) {
        auto it = characters_.find(c);
        if (it != characters_.end()) {
            w += (it->second.advance >> 6);
        }
    }
    return w;
}

void FontRenderer::shutdown() {
    for (auto& pair : characters_) {
        if (pair.second.texture_id) {
            glDeleteTextures(1, &pair.second.texture_id);
        }
    }
    characters_.clear();

    if (face_) { FT_Done_Face(face_); face_ = nullptr; }
    if (ft_) { FT_Done_FreeType(ft_); ft_ = nullptr; }
    if (vao_) { glDeleteVertexArrays(1, &vao_); vao_ = 0; }
    if (vbo_) { glDeleteBuffers(1, &vbo_); vbo_ = 0; }
    if (program_) { glDeleteProgram(program_); program_ = 0; }
    is_ready_ = false;
}

} // namespace aqua
