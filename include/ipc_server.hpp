#pragma once

#include "aqua_protocol.h"
#include "compositor.hpp"
#include <string>
#include <vector>
#include <memory>
#include <map>
#include <EGL/egl.h>
#include <EGL/eglext.h>

namespace aqua {

struct ClientConnection {
    int fd{-1};
    uint32_t client_id{0};
    uint32_t window_id{0};
    std::shared_ptr<Window> window{nullptr};
    GLuint texture_id{0};
    EGLImageKHR egl_image{EGL_NO_IMAGE_KHR};

    // Obsługa pamięci współdzielonej (SHM):
    int shm_fd{-1};
    void* shm_data{nullptr};
    size_t shm_size{0};
    uint32_t shm_w{0};
    uint32_t shm_h{0};
    uint32_t shm_stride{0};
    bool is_shm{false};
};

class IpcServer {
public:
    IpcServer();
    ~IpcServer();

    bool initialize(WindowCompositor* compositor, EGLDisplay egl_dpy, const std::string& socket_path = AQUA_DEFAULT_SOCKET_PATH);
    void dispatch_events();
    void shutdown();

    int server_fd() const { return server_fd_; }
    const std::vector<ClientConnection>& clients() const { return clients_; }

    void send_window_resized(uint32_t window_id, uint32_t width, uint32_t height);
    void send_input_event(uint32_t window_id, MessageType type, const MsgInputEvent& event);
    void send_window_closed(uint32_t window_id);

private:
    void handle_new_connection();
    bool handle_client_message(ClientConnection& client);
    void cleanup_client(ClientConnection& client);
    GLuint import_dmabuf_to_texture(int dmabuf_fd, uint32_t width, uint32_t height, uint32_t stride, uint32_t fourcc, EGLImageKHR& out_img);

    int server_fd_{-1};
    std::string socket_path_;
    WindowCompositor* compositor_{nullptr};
    EGLDisplay egl_dpy_{EGL_NO_DISPLAY};

    std::vector<ClientConnection> clients_;
    uint32_t next_client_id_{1};
    uint32_t next_window_id_{100};
};

} // namespace aqua
