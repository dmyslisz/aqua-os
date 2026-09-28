#pragma once

#include "window.hpp"
#include "aqua_protocol.h"
#include <vector>
#include <memory>
#include <string>

namespace aqua {

class Application {
public:
    Application(int argc, char* argv[], const std::string& app_name = "AquaApp");
    ~Application();

    bool initialize(const std::string& socket_path = AQUA_DEFAULT_SOCKET_PATH);
    void add_window(std::shared_ptr<AppWindow> win);

    int run(int target_fps = 60);
    void quit();

    static Application* instance() { return s_instance_; }

private:
    void dispatch_server_events();
    std::shared_ptr<AppWindow> find_window(uint32_t window_id);

    std::string app_name_;
    int sock_{-1};
    bool running_{false};
    std::vector<std::shared_ptr<AppWindow>> windows_;

    static Application* s_instance_;
};

} // namespace aqua
