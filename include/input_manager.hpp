#pragma once

#include <functional>
#include <string>
#include <libinput.h>

namespace aqua {

struct PointerEvent {
    double dx{0.0};
    double dy{0.0};
    uint32_t button{0};
    bool is_button_press{false};
};

struct KeyEvent {
    uint32_t key{0};
    bool is_press{false};
};

class InputManager {
public:
    using PointerCallback = std::function<void(const PointerEvent&)>;
    using KeyCallback = std::function<void(const KeyEvent&)>;

    InputManager();
    ~InputManager();

    InputManager(const InputManager&) = delete;
    InputManager& operator=(const InputManager&) = delete;

    bool initialize();
    void shutdown();

    // Przetwarzanie oczekujących zdarzeń z libinput
    void dispatch_events();

    int fd() const;

    void set_pointer_callback(PointerCallback cb) { pointer_cb_ = std::move(cb); }
    void set_key_callback(KeyCallback cb) { key_cb_ = std::move(cb); }

private:
    struct libinput* li_{nullptr};
    struct libinput_interface interface_{};

    PointerCallback pointer_cb_;
    KeyCallback key_cb_;

    static int open_restricted(const char* path, int flags, void* user_data);
    static void close_restricted(int fd, void* user_data);
};

} // namespace aqua
