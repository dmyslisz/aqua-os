#include "input_manager.hpp"

#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <iostream>
#include <cstring>
#include <libudev.h>

namespace aqua {

int InputManager::open_restricted(const char* path, int flags, void* /*user_data*/) {
    int fd = open(path, flags | O_CLOEXEC);
    if (fd < 0) {
        std::cerr << "[Aqua Input] Nie mozna otworzyc urzadzenia " << path 
                  << ": " << std::strerror(errno) << std::endl;
    }
    return fd;
}

void InputManager::close_restricted(int fd, void* /*user_data*/) {
    close(fd);
}

InputManager::InputManager() = default;

InputManager::~InputManager() {
    shutdown();
}

bool InputManager::initialize() {
    interface_.open_restricted = open_restricted;
    interface_.close_restricted = close_restricted;

    // Próba 1: Użycie udev context (działa świetnie z libudev-devd na FreeBSD)
    struct udev* udev = udev_new();
    if (udev) {
        li_ = libinput_udev_create_context(&interface_, this, udev);
        if (li_) {
            if (libinput_udev_assign_seat(li_, "seat0") == 0) {
                std::cout << "[Aqua Input] Zainicjalizowano libinput przez udev na seat0." << std::endl;
                udev_unref(udev);
                return true;
            }
            libinput_unref(li_);
            li_ = nullptr;
        }
        udev_unref(udev);
    }

    // Próba 2 (Fallback): Bezpośrednie dodanie urządzeń /dev/input/event*
    std::cout << "[Aqua Input] Fallback: bezposrednie skanowanie /dev/input/..." << std::endl;
    li_ = libinput_path_create_context(&interface_, this);
    if (!li_) {
        std::cerr << "[Aqua Input BŁĄD] Nie udalo sie utworzyc kontekstu libinput!" << std::endl;
        return false;
    }

    DIR* dir = opendir("/dev/input");
    if (dir) {
        struct dirent* entry;
        while ((entry = readdir(dir)) != nullptr) {
            if (std::strncmp(entry->d_name, "event", 5) == 0) {
                std::string dev_path = std::string("/dev/input/") + entry->d_name;
                auto* dev = libinput_path_add_device(li_, dev_path.c_str());
                if (dev) {
                    std::cout << "[Aqua Input] Dodano urzadzenie wejsciowe: " << dev_path 
                              << " (" << libinput_device_get_name(dev) << ")" << std::endl;
                }
            }
        }
        closedir(dir);
    } else {
        std::cerr << "[Aqua Input BŁĄD] Brak katalogu /dev/input! Sprawdz czy moduł evdev/uinput jest aktywny." << std::endl;
        return false;
    }

    return true;
}

int InputManager::fd() const {
    return li_ ? libinput_get_fd(li_) : -1;
}

void InputManager::dispatch_events() {
    if (!li_) return;

    if (libinput_dispatch(li_) != 0) {
        std::cerr << "[Aqua Input BŁĄD] libinput_dispatch nie powiodl sie!" << std::endl;
        return;
    }

    struct libinput_event* ev = nullptr;
    while ((ev = libinput_get_event(li_)) != nullptr) {
        enum libinput_event_type type = libinput_event_get_type(ev);

        switch (type) {
            case LIBINPUT_EVENT_POINTER_MOTION: {
                auto* p = libinput_event_get_pointer_event(ev);
                double dx = libinput_event_pointer_get_dx(p);
                double dy = libinput_event_pointer_get_dy(p);
                if (pointer_cb_) {
                    pointer_cb_({dx, dy, 0, false});
                }
                break;
            }
            case LIBINPUT_EVENT_POINTER_BUTTON: {
                auto* p = libinput_event_get_pointer_event(ev);
                uint32_t btn = libinput_event_pointer_get_button(p);
                bool pressed = libinput_event_pointer_get_button_state(p) == LIBINPUT_BUTTON_STATE_PRESSED;
                if (pointer_cb_) {
                    pointer_cb_({0.0, 0.0, btn, pressed});
                }
                break;
            }
            case LIBINPUT_EVENT_KEYBOARD_KEY: {
                auto* k = libinput_event_get_keyboard_event(ev);
                uint32_t key = libinput_event_keyboard_get_key(k);
                bool pressed = libinput_event_keyboard_get_key_state(k) == LIBINPUT_KEY_STATE_PRESSED;
                if (key_cb_) {
                    key_cb_({key, pressed});
                }
                break;
            }
            default:
                break;
        }

        libinput_event_destroy(ev);
    }
}

void InputManager::shutdown() {
    if (li_) {
        libinput_unref(li_);
        li_ = nullptr;
    }
}

} // namespace aqua
