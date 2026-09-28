#include "aqua/aqua.hpp"
#include <iostream>
#include <string>
#include <vector>
#include <iomanip>
#include <sstream>
#include <cmath>

namespace {

enum class Op {
    None,
    Add,
    Sub,
    Mul,
    Div
};

struct CalcButton {
    std::string label;
    int col;
    int row;
    int col_span{1};
    aqua::Color bg_color;
    aqua::Color bg_pressed;
    aqua::Color text_color;
    int font_scale{2};
};

} // namespace

class CalcWindow : public aqua::AppWindow {
public:
    CalcWindow() : aqua::AppWindow("Calculator", 300, 440) {
        init_buttons();
    }

    void on_draw(aqua::Canvas& canvas) override {
        // 1. Tło okna (macOS Dark Slate)
        canvas.clear(aqua::Color::MacDarkBg);

        // Subtelna linia pod obszarem wyświetlacza
        canvas.fill_rect(10, 115, width() - 20, 1, aqua::Color::hex(0x3A3A3C));

        // 2. Obszar wyświetlacza
        // Wyświetlanie bieżącej operacji u góry
        if (pending_op_ != Op::None && has_operand_) {
            std::string op_sym = "+";
            if (pending_op_ == Op::Sub) op_sym = "-";
            else if (pending_op_ == Op::Mul) op_sym = "x";
            else if (pending_op_ == Op::Div) op_sym = "/";
            std::string prev_str = format_number(operand_) + " " + op_sym;
            canvas.draw_text_right(width() - 20, 42, prev_str, aqua::Color::hex(0x8E8E93), 1);
        }

        // Główny wynik (duży tekst z automatycznym skalowaniem)
        std::string disp = display_text_;
        int scale = 3;
        if (disp.length() > 9) scale = 2;
        if (disp.length() > 14) scale = 1;

        int text_y = (scale == 3) ? 62 : ((scale == 2) ? 72 : 82);
        canvas.draw_text_right(width() - 18, text_y, disp, aqua::Color::White, scale);

        // 3. Rysowanie przycisków
        const int start_x = 14;
        const int start_y = 126;
        const int btn_w = 60;
        const int btn_h = 52;
        const int gap = 8;
        const int radius = 14;

        for (size_t i = 0; i < buttons_.size(); ++i) {
            const auto& btn = buttons_[i];
            int bx = start_x + btn.col * (btn_w + gap);
            int by = start_y + btn.row * (btn_h + gap);
            int bw = (btn.col_span == 2) ? (btn_w * 2 + gap) : btn_w;

            bool is_pressed = (pressed_button_idx_ == static_cast<int>(i));

            // Sprawdź czy ten przycisk to aktywny operator
            bool is_active_op = false;
            if (pending_op_ == Op::Div && btn.label == "/") is_active_op = true;
            if (pending_op_ == Op::Mul && btn.label == "x") is_active_op = true;
            if (pending_op_ == Op::Sub && btn.label == "-") is_active_op = true;
            if (pending_op_ == Op::Add && btn.label == "+") is_active_op = true;

            aqua::Color bg = is_pressed ? btn.bg_pressed : btn.bg_color;
            aqua::Color tc = btn.text_color;

            if (is_active_op && !is_pressed) {
                // macOS wyróżnia aktywny operator odwróconymi kolorami
                bg = aqua::Color::White;
                tc = aqua::Color::MacOrange;
            }

            canvas.fill_rounded_rect(bx, by, bw, btn_h, radius, bg);

            // Wyśrodkowany tekst przycisku
            int cx = bx + bw / 2;
            int cy = by + btn_h / 2;
            canvas.draw_text_centered(cx, cy, btn.label, tc, btn.font_scale);
        }
    }

    void on_mouse_down(float x, float y, uint32_t button) override {
        if (button != 272) return; // BTN_LEFT

        int idx = hit_test_button(static_cast<int>(x), static_cast<int>(y));
        if (idx >= 0) {
            pressed_button_idx_ = idx;
            request_redraw();
        }
    }

    void on_mouse_up(float x, float y, uint32_t button) override {
        if (button != 272) return;

        if (pressed_button_idx_ >= 0) {
            int idx = hit_test_button(static_cast<int>(x), static_cast<int>(y));
            if (idx == pressed_button_idx_) {
                execute_button(buttons_[idx].label);
            }
            pressed_button_idx_ = -1;
            request_redraw();
        }
    }

    void on_key_down(uint32_t key_code) override {
        // Obsługa klawiszy klawiatury
        // kody evdev:
        // 11 = 0, 2..10 = 1..9
        // 52 = . (dot)
        // 14 = Backspace
        // 1 = ESC
        // 28 = Enter, 96 = KP_ENTER
        // 78 = KP_PLUS, 74 = KP_MINUS, 55 = KP_ASTERISK, 98 = KP_SLASH, 82 = KP_0
        if (key_code >= 2 && key_code <= 10) {
            char d = '0' + (key_code - 1);
            execute_button(std::string(1, d));
        } else if (key_code == 11 || key_code == 82) {
            execute_button("0");
        } else if (key_code >= 79 && key_code <= 81) { // KP_1..KP_3
            char d = '1' + (key_code - 79);
            execute_button(std::string(1, d));
        } else if (key_code >= 75 && key_code <= 77) { // KP_4..KP_6
            char d = '4' + (key_code - 75);
            execute_button(std::string(1, d));
        } else if (key_code >= 71 && key_code <= 73) { // KP_7..KP_9
            char d = '7' + (key_code - 71);
            execute_button(std::string(1, d));
        } else if (key_code == 52 || key_code == 83) { // Dot or KP_Dot
            execute_button(".");
        } else if (key_code == 14) { // Backspace
            backspace();
        } else if (key_code == 1) { // ESC -> AC
            execute_button("AC");
        } else if (key_code == 28 || key_code == 96) { // Enter
            execute_button("=");
        } else if (key_code == 78) { // KP_+
            execute_button("+");
        } else if (key_code == 74) { // KP_-
            execute_button("-");
        } else if (key_code == 55) { // KP_*
            execute_button("x");
        } else if (key_code == 98) { // KP_/
            execute_button("/");
        }

        request_redraw();
    }

private:
    void init_buttons() {
        buttons_.clear();

        auto btn_fn = aqua::Color::hex(0xA5A5A5);
        auto btn_fn_p = aqua::Color::hex(0xD4D4D2);
        auto btn_num = aqua::Color::hex(0x3A3A3C);
        auto btn_num_p = aqua::Color::hex(0x5A5A5C);
        auto btn_op = aqua::Color::MacOrange;
        auto btn_op_p = aqua::Color::MacOrangePressed;

        // Wiersz 0
        buttons_.push_back({"AC",  0, 0, 1, btn_fn,  btn_fn_p,  aqua::Color::Black, 1});
        buttons_.push_back({"+/-", 1, 0, 1, btn_fn,  btn_fn_p,  aqua::Color::Black, 1});
        buttons_.push_back({"%",   2, 0, 1, btn_fn,  btn_fn_p,  aqua::Color::Black, 1});
        buttons_.push_back({"/",   3, 0, 1, btn_op,  btn_op_p,  aqua::Color::White, 2});

        // Wiersz 1
        buttons_.push_back({"7",   0, 1, 1, btn_num, btn_num_p, aqua::Color::White, 2});
        buttons_.push_back({"8",   1, 1, 1, btn_num, btn_num_p, aqua::Color::White, 2});
        buttons_.push_back({"9",   2, 1, 1, btn_num, btn_num_p, aqua::Color::White, 2});
        buttons_.push_back({"x",   3, 1, 1, btn_op,  btn_op_p,  aqua::Color::White, 2});

        // Wiersz 2
        buttons_.push_back({"4",   0, 2, 1, btn_num, btn_num_p, aqua::Color::White, 2});
        buttons_.push_back({"5",   1, 2, 1, btn_num, btn_num_p, aqua::Color::White, 2});
        buttons_.push_back({"6",   2, 2, 1, btn_num, btn_num_p, aqua::Color::White, 2});
        buttons_.push_back({"-",   3, 2, 1, btn_op,  btn_op_p,  aqua::Color::White, 2});

        // Wiersz 3
        buttons_.push_back({"1",   0, 3, 1, btn_num, btn_num_p, aqua::Color::White, 2});
        buttons_.push_back({"2",   1, 3, 1, btn_num, btn_num_p, aqua::Color::White, 2});
        buttons_.push_back({"3",   2, 3, 1, btn_num, btn_num_p, aqua::Color::White, 2});
        buttons_.push_back({"+",   3, 3, 1, btn_op,  btn_op_p,  aqua::Color::White, 2});

        // Wiersz 4
        buttons_.push_back({"0",   0, 4, 2, btn_num, btn_num_p, aqua::Color::White, 2});
        buttons_.push_back({".",   2, 4, 1, btn_num, btn_num_p, aqua::Color::White, 2});
        buttons_.push_back({"=",   3, 4, 1, btn_op,  btn_op_p,  aqua::Color::White, 2});
    }

    int hit_test_button(int px, int py) const {
        const int start_x = 14;
        const int start_y = 126;
        const int btn_w = 60;
        const int btn_h = 52;
        const int gap = 8;

        for (size_t i = 0; i < buttons_.size(); ++i) {
            const auto& btn = buttons_[i];
            int bx = start_x + btn.col * (btn_w + gap);
            int by = start_y + btn.row * (btn_h + gap);
            int bw = (btn.col_span == 2) ? (btn_w * 2 + gap) : btn_w;

            if (px >= bx && px <= bx + bw && py >= by && py <= by + btn_h) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    void execute_button(const std::string& label) {
        if (label >= "0" && label <= "9") {
            append_digit(label[0]);
        } else if (label == ".") {
            append_dot();
        } else if (label == "AC") {
            clear_all();
        } else if (label == "+/-") {
            toggle_sign();
        } else if (label == "%") {
            apply_percent();
        } else if (label == "+") {
            set_op(Op::Add);
        } else if (label == "-") {
            set_op(Op::Sub);
        } else if (label == "x") {
            set_op(Op::Mul);
        } else if (label == "/") {
            set_op(Op::Div);
        } else if (label == "=") {
            calculate_result();
        }
    }

    void append_digit(char d) {
        if (clear_on_next_digit_) {
            display_text_ = "0";
            clear_on_next_digit_ = false;
        }

        if (display_text_ == "0") {
            display_text_ = std::string(1, d);
        } else if (display_text_.length() < 16) {
            display_text_ += d;
        }
    }

    void append_dot() {
        if (clear_on_next_digit_) {
            display_text_ = "0.";
            clear_on_next_digit_ = false;
            return;
        }

        if (display_text_.find('.') == std::string::npos) {
            display_text_ += ".";
        }
    }

    void backspace() {
        if (clear_on_next_digit_) return;
        if (display_text_.length() > 1) {
            display_text_.pop_back();
        } else {
            display_text_ = "0";
        }
    }

    void clear_all() {
        display_text_ = "0";
        operand_ = 0.0;
        has_operand_ = false;
        pending_op_ = Op::None;
        clear_on_next_digit_ = false;
    }

    void toggle_sign() {
        if (display_text_ == "0") return;
        if (display_text_.front() == '-') {
            display_text_.erase(0, 1);
        } else {
            display_text_.insert(0, "-");
        }
    }

    void apply_percent() {
        double val = std::stod(display_text_);
        val /= 100.0;
        display_text_ = format_number(val);
    }

    void set_op(Op op) {
        if (pending_op_ != Op::None && !clear_on_next_digit_) {
            calculate_result();
        }
        operand_ = std::stod(display_text_);
        has_operand_ = true;
        pending_op_ = op;
        clear_on_next_digit_ = true;
    }

    void calculate_result() {
        if (pending_op_ == Op::None || !has_operand_) return;

        double current = std::stod(display_text_);
        double res = 0.0;

        switch (pending_op_) {
            case Op::Add: res = operand_ + current; break;
            case Op::Sub: res = operand_ - current; break;
            case Op::Mul: res = operand_ * current; break;
            case Op::Div:
                if (std::abs(current) < 1e-12) {
                    display_text_ = "Error";
                    has_operand_ = false;
                    pending_op_ = Op::None;
                    clear_on_next_digit_ = true;
                    return;
                }
                res = operand_ / current;
                break;
            default: break;
        }

        display_text_ = format_number(res);
        operand_ = res;
        pending_op_ = Op::None;
        clear_on_next_digit_ = true;
    }

    std::string format_number(double num) {
        if (std::isnan(num) || std::isinf(num)) return "Error";
        std::ostringstream ss;
        ss << std::setprecision(10) << num;
        return ss.str();
    }

    std::vector<CalcButton> buttons_;
    std::string display_text_{"0"};
    double operand_{0.0};
    bool has_operand_{false};
    Op pending_op_{Op::None};
    bool clear_on_next_digit_{false};
    int pressed_button_idx_{-1};
};

int main(int argc, char* argv[]) {
    aqua::Application app(argc, argv, "Calculator");
    if (!app.initialize()) {
        return 1;
    }

    auto win = std::make_shared<CalcWindow>();
    app.add_window(win);

    return app.run();
}
