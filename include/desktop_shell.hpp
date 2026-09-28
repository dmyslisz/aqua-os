#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <GLES3/gl3.h>

namespace aqua {

struct DockItem {
    std::string name;
    std::string icon_char;
    uint32_t color{0xFFFFFFFF};
    bool is_running{false};
    float current_size{48.0f};
    float base_x{0.0f};
};

class DesktopShell {
public:
    DesktopShell();
    ~DesktopShell();

    bool initialize(uint32_t screen_width, uint32_t screen_height);
    void update_screen_size(uint32_t screen_width, uint32_t screen_height);

    void handle_pointer_move(float cursor_x, float cursor_y);
    int handle_pointer_click(float cursor_x, float cursor_y); // Zwraca indeks klikniętej ikony Docka (-1 jeśli brak)

    void render(float cursor_x, float cursor_y, float elapsed_time);
    void shutdown();

    static constexpr float TOP_BAR_HEIGHT = 28.0f;
    static constexpr float DOCK_BASE_ICON_SIZE = 48.0f;
    static constexpr float DOCK_MAX_ICON_SIZE = 76.0f;
    static constexpr float DOCK_PADDING = 10.0f;

private:
    void render_top_bar(float elapsed_time);
    void render_dock(float cursor_x, float cursor_y);

    uint32_t screen_w_{1920};
    uint32_t screen_h_{1080};

    float cursor_x_{0.0f};
    float cursor_y_{0.0f};

    std::vector<DockItem> dock_items_;
    std::unique_ptr<class FontRenderer> font_;
    std::unique_ptr<class StatusIconRenderer> status_icons_;

    // Shadery
    GLuint shell_program_{0};
    GLuint vao_{0};
    GLuint vbo_{0};

    // Uniformy
    GLint u_screen_size_{-1};
    GLint u_rect_{-1};
    GLint u_color_{-1};
    GLint u_radius_{-1};
    GLint u_type_{-1}; // 0 = Top bar, 1 = Dock body, 2 = Dock icon, 3 = Active dot
    GLint u_border_color_{-1};
};

} // namespace aqua
