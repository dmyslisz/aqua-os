#include "aqua/application.hpp"
#include "aqua/window.hpp"
#include "aqua/canvas.hpp"
#include "aqua/color.hpp"
#include "aqua/font_8x16.hpp"

#include <iostream>
#include <vector>
#include <string>
#include <algorithm>
#include <sstream>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <signal.h>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <poll.h>

namespace aqua {

// Standardowa paleta 16 kolorów ANSI (zgodna z macOS Terminal Dark)
static const Color ANSI_PALETTE[16] = {
    Color(24, 24, 26, 255),    // 0: Black
    Color(235, 77, 75, 255),   // 1: Red
    Color(46, 204, 113, 255),  // 2: Green
    Color(241, 196, 15, 255),  // 3: Yellow
    Color(52, 152, 219, 255),  // 4: Blue
    Color(155, 89, 182, 255),  // 5: Magenta
    Color(26, 188, 156, 255),  // 6: Cyan
    Color(215, 215, 220, 255), // 7: White / Light Gray
    Color(90, 90, 95, 255),    // 8: Bright Black (Dark Gray)
    Color(255, 107, 107, 255), // 9: Bright Red
    Color(46, 213, 115, 255),  // 10: Bright Green
    Color(255, 234, 167, 255), // 11: Bright Yellow
    Color(112, 161, 255, 255), // 12: Bright Blue
    Color(224, 86, 253, 255),  // 13: Bright Magenta
    Color(126, 255, 245, 255), // 14: Bright Cyan
    Color(255, 255, 255, 255)  // 15: Bright White
};

struct TermCell {
    char c = ' ';
    uint8_t fg = 7;
    uint8_t bg = 255; // 255 = domyślne przezroczyste tło
    bool bold = false;
    bool reverse = false;
};

class TerminalWindow : public AppWindow {
public:
    TerminalWindow(uint32_t width = 780, uint32_t height = 480)
        : AppWindow("Terminal — zsh", width, height) {
        recalculate_grid();
    }

    ~TerminalWindow() override {
        cleanup_pty();
    }

    void on_create() override {
        if (!open_pty()) {
            std::cerr << "[Terminal] Nie udalo sie otworzyc pseudo-terminala PTY!" << std::endl;
        }
    }

    void on_resize(uint32_t new_width, uint32_t new_height) override {
        (void)new_width;
        (void)new_height;
        recalculate_grid();
        notify_pty_size();
        request_redraw();
    }

    void on_update(float dt) override {
        // 1. Miganie kursora co 500 ms
        blink_timer_ += dt;
        if (blink_timer_ >= 0.5f) {
            blink_timer_ = 0.0f;
            cursor_visible_ = !cursor_visible_;
            request_redraw();
        }

        // 2. Odczyt danych z powłoki PTY
        if (pty_master_ >= 0) {
            char buf[4096];
            bool has_read = false;

            while (true) {
                ssize_t n = read(pty_master_, buf, sizeof(buf));
                if (n > 0) {
                    has_read = true;
                    for (ssize_t i = 0; i < n; ++i) {
                        process_char(buf[i]);
                    }
                } else {
                    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                        break;
                    }
                    if (n == 0 || (n < 0 && errno == EIO)) {
                        close();
                    }
                    break;
                }
            }

            if (has_read) {
                request_redraw();
            }
        }

        // 3. Sprawdź czy proces potomny jeszcze żyje
        if (child_pid_ > 0) {
            int status = 0;
            pid_t res = waitpid(child_pid_, &status, WNOHANG);
            if (res > 0) {
                close();
            }
        }
    }

    void on_key_down(uint32_t key_code) override {
        if (key_code == 42 || key_code == 54) { shift_down_ = true; return; }
        if (key_code == 29 || key_code == 97) { ctrl_down_ = true; return; }
        if (key_code == 56 || key_code == 100) { alt_down_ = true; return; }

        if (pty_master_ < 0) return;

        std::string out = translate_key(key_code);
        if (!out.empty()) {
            write(pty_master_, out.data(), out.size());
            cursor_visible_ = true;
            blink_timer_ = 0.0f;
            request_redraw();
        }
    }

    void on_key_up(uint32_t key_code) override {
        if (key_code == 42 || key_code == 54) { shift_down_ = false; return; }
        if (key_code == 29 || key_code == 97) { ctrl_down_ = false; return; }
        if (key_code == 56 || key_code == 100) { alt_down_ = false; return; }
    }

    void on_close() override {
        cleanup_pty();
    }

    void on_draw(Canvas& canvas) override {
        // Tło okna Terminala: macOS Dark Glass (półprzezroczysty grafit)
        canvas.clear(Color(18, 18, 20, 225));

        const int char_w = 9;
        const int char_h = 18;
        const int pad_x = 12;
        const int pad_y = 34;

        for (int r = 0; r < rows_; ++r) {
            int py = pad_y + r * char_h;
            if (py + char_h > static_cast<int>(height())) break;

            for (int c = 0; c < cols_; ++c) {
                int px = pad_x + c * char_w;
                if (px + char_w > static_cast<int>(width())) break;

                const auto& cell = grid_[r][c];

                if (cell.bg < 16) {
                    canvas.fill_rect(px, py, char_w, char_h, ANSI_PALETTE[cell.bg]);
                }

                if (cell.c != ' ' && static_cast<unsigned char>(cell.c) >= 32) {
                    Color fg_col = (cell.fg < 16) ? ANSI_PALETTE[cell.fg] : Color(220, 220, 225, 255);
                    if (cell.reverse) {
                        fg_col = (cell.bg < 16) ? ANSI_PALETTE[cell.bg] : Color(20, 20, 20, 255);
                    }
                    canvas.draw_char(px + 1, py + 1, cell.c, fg_col);
                }
            }
        }

        // Rysowanie kursora tekstowego macOS
        if (cursor_visible_ && cursor_y_ >= 0 && cursor_y_ < rows_ && cursor_x_ >= 0 && cursor_x_ < cols_) {
            int cx = pad_x + cursor_x_ * char_w;
            int cy = pad_y + cursor_y_ * char_h;

            Color cursor_col(82, 139, 255, 210);
            canvas.fill_rect(cx, cy, char_w, char_h, cursor_col);

            char under_c = grid_[cursor_y_][cursor_x_].c;
            if (under_c != ' ' && static_cast<unsigned char>(under_c) >= 32) {
                canvas.draw_char(cx + 1, cy + 1, under_c, Color(10, 10, 15, 255));
            }
        }
    }

private:
    enum class ParserState {
        Normal,
        Esc,
        CSI,
        OSC
    };

    int cols_{80};
    int rows_{24};
    int cursor_x_{0};
    int cursor_y_{0};
    int saved_x_{0};
    int saved_y_{0};

    bool cursor_visible_{true};
    float blink_timer_{0.0f};

    bool shift_down_{false};
    bool ctrl_down_{false};
    bool alt_down_{false};

    int pty_master_{-1};
    pid_t child_pid_{-1};
    std::vector<std::vector<TermCell>> grid_;

    ParserState parser_state_{ParserState::Normal};
    std::string csi_params_;
    std::string osc_string_;

    uint8_t cur_fg_{7};
    uint8_t cur_bg_{255};
    bool cur_bold_{false};
    bool cur_reverse_{false};

    void recalculate_grid() {
        const int char_w = 9;
        const int char_h = 18;
        const int pad_x = 12;
        const int pad_y = 34;

        int new_cols = std::max(20, static_cast<int>(width() - pad_x * 2) / char_w);
        int new_rows = std::max(5, static_cast<int>(height() - pad_y - 10) / char_h);

        std::vector<std::vector<TermCell>> new_grid(new_rows, std::vector<TermCell>(new_cols));

        for (int r = 0; r < std::min(rows_, new_rows); ++r) {
            for (int c = 0; c < std::min(cols_, new_cols); ++c) {
                new_grid[r][c] = grid_[r][c];
            }
        }

        cols_ = new_cols;
        rows_ = new_rows;
        grid_ = std::move(new_grid);

        cursor_x_ = std::clamp(cursor_x_, 0, cols_ - 1);
        cursor_y_ = std::clamp(cursor_y_, 0, rows_ - 1);
    }

    void notify_pty_size() {
        if (pty_master_ < 0) return;
        struct winsize ws{};
        ws.ws_col = static_cast<unsigned short>(cols_);
        ws.ws_row = static_cast<unsigned short>(rows_);
        ioctl(pty_master_, TIOCSWINSZ, &ws);
        if (child_pid_ > 0) {
            ::kill(child_pid_, SIGWINCH);
        }
    }

    bool open_pty() {
        pty_master_ = posix_openpt(O_RDWR | O_NOCTTY);
        if (pty_master_ < 0) return false;

        if (grantpt(pty_master_) != 0 || unlockpt(pty_master_) != 0) {
            ::close(pty_master_);
            pty_master_ = -1;
            return false;
        }

        const char* pts_name = ptsname(pty_master_);
        if (!pts_name) {
            ::close(pty_master_);
            pty_master_ = -1;
            return false;
        }

        int slave_fd = open(pts_name, O_RDWR | O_NOCTTY);
        if (slave_fd < 0) {
            ::close(pty_master_);
            pty_master_ = -1;
            return false;
        }

        child_pid_ = fork();
        if (child_pid_ < 0) {
            ::close(slave_fd);
            ::close(pty_master_);
            pty_master_ = -1;
            return false;
        }

        if (child_pid_ == 0) {
            ::close(pty_master_);
            setsid();
#if defined(TIOCSCTTY)
            ioctl(slave_fd, TIOCSCTTY, 0);
#endif
            dup2(slave_fd, STDIN_FILENO);
            dup2(slave_fd, STDOUT_FILENO);
            dup2(slave_fd, STDERR_FILENO);
            ::close(slave_fd);

            struct winsize ws{};
            ws.ws_col = static_cast<unsigned short>(cols_);
            ws.ws_row = static_cast<unsigned short>(rows_);
            ioctl(STDIN_FILENO, TIOCSWINSZ, &ws);

            setenv("TERM", "xterm-256color", 1);
            setenv("COLORTERM", "truecolor", 1);
            setenv("LANG", "C.UTF-8", 1);

            const char* shell = getenv("SHELL");
            if (!shell || !*shell) {
                if (access("/usr/local/bin/zsh", X_OK) == 0) shell = "/usr/local/bin/zsh";
                else if (access("/bin/csh", X_OK) == 0) shell = "/bin/csh";
                else shell = "/bin/sh";
            }
            execl(shell, shell, nullptr);
            _exit(127);
        }

        ::close(slave_fd);
        int flags = fcntl(pty_master_, F_GETFL, 0);
        fcntl(pty_master_, F_SETFL, flags | O_NONBLOCK);
        return true;
    }

    void cleanup_pty() {
        if (child_pid_ > 0) {
            ::kill(child_pid_, SIGHUP);
            int status = 0;
            waitpid(child_pid_, &status, WNOHANG);
            child_pid_ = -1;
        }
        if (pty_master_ >= 0) {
            ::close(pty_master_);
            pty_master_ = -1;
        }
    }

    void scroll_up() {
        for (int r = 0; r < rows_ - 1; ++r) {
            grid_[r] = grid_[r + 1];
        }
        for (int c = 0; c < cols_; ++c) {
            grid_[rows_ - 1][c] = TermCell{' ', cur_fg_, cur_bg_, false, false};
        }
        cursor_y_ = rows_ - 1;
    }

    void process_char(char c) {
        switch (parser_state_) {
            case ParserState::Normal:
                if (c == '\033') {
                    parser_state_ = ParserState::Esc;
                } else if (c == '\r') {
                    cursor_x_ = 0;
                } else if (c == '\n') {
                    cursor_y_++;
                    if (cursor_y_ >= rows_) scroll_up();
                } else if (c == '\b') {
                    if (cursor_x_ > 0) cursor_x_--;
                } else if (c == '\t') {
                    cursor_x_ = (cursor_x_ + 8) & ~7;
                    if (cursor_x_ >= cols_) cursor_x_ = cols_ - 1;
                } else if (c == '\a') {
                    // Dzwonek / Bell (ignoruj)
                } else if (static_cast<unsigned char>(c) >= 32) {
                    if (cursor_x_ >= cols_) {
                        cursor_x_ = 0;
                        cursor_y_++;
                        if (cursor_y_ >= rows_) scroll_up();
                    }
                    grid_[cursor_y_][cursor_x_] = TermCell{c, cur_fg_, cur_bg_, cur_bold_, cur_reverse_};
                    cursor_x_++;
                }
                break;

            case ParserState::Esc:
                if (c == '[') {
                    csi_params_.clear();
                    parser_state_ = ParserState::CSI;
                } else if (c == ']') {
                    osc_string_.clear();
                    parser_state_ = ParserState::OSC;
                } else if (c == '7') {
                    saved_x_ = cursor_x_;
                    saved_y_ = cursor_y_;
                    parser_state_ = ParserState::Normal;
                } else if (c == '8') {
                    cursor_x_ = saved_x_;
                    cursor_y_ = saved_y_;
                    parser_state_ = ParserState::Normal;
                } else {
                    parser_state_ = ParserState::Normal;
                }
                break;

            case ParserState::CSI:
                if ((c >= '0' && c <= '9') || c == ';' || c == '?' || c == ' ') {
                    csi_params_.push_back(c);
                } else {
                    execute_csi(c);
                    parser_state_ = ParserState::Normal;
                }
                break;

            case ParserState::OSC:
                if (c == '\007' || c == '\033') {
                    parser_state_ = ParserState::Normal;
                } else {
                    osc_string_.push_back(c);
                }
                break;
        }
    }

    void execute_csi(char cmd) {
        std::vector<int> params;
        std::stringstream ss(csi_params_);
        std::string seg;
        while (std::getline(ss, seg, ';')) {
            if (!seg.empty()) {
                try {
                    params.push_back(std::stoi(seg));
                } catch (...) {
                    params.push_back(0);
                }
            } else {
                params.push_back(0);
            }
        }
        if (params.empty()) params.push_back(0);

        int p0 = params.size() > 0 ? params[0] : 0;
        int p1 = params.size() > 1 ? params[1] : 0;

        switch (cmd) {
            case 'A': { // Cursor Up
                int count = std::max(1, p0);
                cursor_y_ = std::max(0, cursor_y_ - count);
                break;
            }
            case 'B': { // Cursor Down
                int count = std::max(1, p0);
                cursor_y_ = std::min(rows_ - 1, cursor_y_ + count);
                break;
            }
            case 'C': { // Cursor Right
                int count = std::max(1, p0);
                cursor_x_ = std::min(cols_ - 1, cursor_x_ + count);
                break;
            }
            case 'D': { // Cursor Left
                int count = std::max(1, p0);
                cursor_x_ = std::max(0, cursor_x_ - count);
                break;
            }
            case 'H':
            case 'f': { // Cursor Position
                cursor_y_ = std::clamp((p0 > 0 ? p0 - 1 : 0), 0, rows_ - 1);
                cursor_x_ = std::clamp((p1 > 0 ? p1 - 1 : 0), 0, cols_ - 1);
                break;
            }
            case 'J': { // Erase in Display
                if (p0 == 2 || p0 == 3) {
                    for (int r = 0; r < rows_; ++r) {
                        for (int c = 0; c < cols_; ++c) {
                            grid_[r][c] = TermCell{' ', cur_fg_, 255, false, false};
                        }
                    }
                    cursor_x_ = 0;
                    cursor_y_ = 0;
                } else if (p0 == 0) {
                    for (int c = cursor_x_; c < cols_; ++c) grid_[cursor_y_][c] = TermCell{' ', cur_fg_, 255, false, false};
                    for (int r = cursor_y_ + 1; r < rows_; ++r) {
                        for (int c = 0; c < cols_; ++c) grid_[r][c] = TermCell{' ', cur_fg_, 255, false, false};
                    }
                }
                break;
            }
            case 'K': { // Erase in Line
                if (p0 == 0) {
                    for (int c = cursor_x_; c < cols_; ++c) grid_[cursor_y_][c] = TermCell{' ', cur_fg_, 255, false, false};
                } else if (p0 == 1) {
                    for (int c = 0; c <= cursor_x_; ++c) grid_[cursor_y_][c] = TermCell{' ', cur_fg_, 255, false, false};
                } else if (p0 == 2) {
                    for (int c = 0; c < cols_; ++c) grid_[cursor_y_][c] = TermCell{' ', cur_fg_, 255, false, false};
                }
                break;
            }
            case 'm': { // SGR - Kolory i style
                for (size_t i = 0; i < params.size(); ++i) {
                    int p = params[i];
                    if (p == 0) {
                        cur_fg_ = 7;
                        cur_bg_ = 255;
                        cur_bold_ = false;
                        cur_reverse_ = false;
                    } else if (p == 1) {
                        cur_bold_ = true;
                    } else if (p == 7) {
                        cur_reverse_ = true;
                    } else if (p >= 30 && p <= 37) {
                        cur_fg_ = p - 30;
                    } else if (p == 39) {
                        cur_fg_ = 7;
                    } else if (p >= 40 && p <= 47) {
                        cur_bg_ = p - 40;
                    } else if (p == 49) {
                        cur_bg_ = 255;
                    } else if (p >= 90 && p <= 97) {
                        cur_fg_ = 8 + (p - 90);
                    } else if (p >= 100 && p <= 107) {
                        cur_bg_ = 8 + (p - 100);
                    } else if (p == 38 && (i + 2 < params.size()) && params[i + 1] == 5) {
                        cur_fg_ = params[i + 2] % 16;
                        i += 2;
                    } else if (p == 48 && (i + 2 < params.size()) && params[i + 1] == 5) {
                        cur_bg_ = params[i + 2] % 16;
                        i += 2;
                    }
                }
                break;
            }
        }
    }

    std::string translate_key(uint32_t code) {
        if (code == 14) return "\x7f"; // Backspace
        if (code == 28 || code == 96) return "\r"; // Enter
        if (code == 15) return "\t"; // Tab
        if (code == 1) return "\033"; // Escape
        if (code == 57) return " "; // Spacja

        if (code == 103) return "\033[A"; // Up
        if (code == 108) return "\033[B"; // Down
        if (code == 106) return "\033[C"; // Right
        if (code == 105) return "\033[D"; // Left
        if (code == 102) return "\033[H"; // Home
        if (code == 107) return "\033[F"; // End
        if (code == 104) return "\033[5~"; // Page Up
        if (code == 109) return "\033[6~"; // Page Down
        if (code == 111) return "\033[3~"; // Delete

        static const struct { uint32_t code; char normal; char shifted; } LETTER_MAP[] = {
            {16, 'q', 'Q'}, {17, 'w', 'W'}, {18, 'e', 'E'}, {19, 'r', 'R'}, {20, 't', 'T'},
            {21, 'y', 'Y'}, {22, 'u', 'U'}, {23, 'i', 'I'}, {24, 'o', 'O'}, {25, 'p', 'P'},
            {30, 'a', 'A'}, {31, 's', 'S'}, {32, 'd', 'D'}, {33, 'f', 'F'}, {34, 'g', 'G'},
            {35, 'h', 'H'}, {36, 'j', 'J'}, {37, 'k', 'K'}, {38, 'l', 'L'},
            {44, 'z', 'Z'}, {45, 'x', 'X'}, {46, 'c', 'C'}, {47, 'v', 'V'}, {48, 'b', 'B'},
            {49, 'n', 'N'}, {50, 'm', 'M'}
        };

        for (const auto& item : LETTER_MAP) {
            if (item.code == code) {
                if (ctrl_down_) {
                    char c = static_cast<char>(item.normal - 'a' + 1);
                    return std::string(1, c);
                }
                return std::string(1, shift_down_ ? item.shifted : item.normal);
            }
        }

        static const struct { uint32_t code; char normal; char shifted; } NUM_MAP[] = {
            {2, '1', '!'}, {3, '2', '@'}, {4, '3', '#'}, {5, '4', '$'}, {6, '5', '%'},
            {7, '6', '^'}, {8, '7', '&'}, {9, '8', '*'}, {10, '9', '('}, {11, '0', ')'},
            {12, '-', '_'}, {13, '=', '+'}, {26, '[', '{'}, {27, ']', '}'},
            {39, ';', ':'}, {40, '\'', '\"'}, {41, '`', '~'}, {43, '\\', '|'},
            {51, ',', '<'}, {52, '.', '>'}, {53, '/', '?'}
        };

        for (const auto& item : NUM_MAP) {
            if (item.code == code) {
                if (ctrl_down_) {
                    if (code == 12) return "\x1f"; // Ctrl+-
                    if (code == 26) return "\x1b"; // Ctrl+[
                    if (code == 27) return "\x1d"; // Ctrl+]
                    if (code == 43) return "\x1c"; // Ctrl+Backslash
                }
                return std::string(1, shift_down_ ? item.shifted : item.normal);
            }
        }

        return "";
    }
};

} // namespace aqua

int main(int argc, char* argv[]) {
    aqua::Application app(argc, argv, "Terminal");
    if (!app.initialize()) {
        std::cerr << "[Terminal] Blad polaczenia z aqua-server!" << std::endl;
        return 1;
    }

    auto win = std::make_shared<aqua::TerminalWindow>(780, 480);
    app.add_window(win);

    return app.run();
}
