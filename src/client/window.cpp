#include "aqua/window.hpp"
#include "aqua_protocol.h"
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <iostream>
#include <algorithm>

namespace aqua {

namespace {

template <typename T>
int send_fd_with_payload(int sock, int fd_to_send, const MsgHeader& hdr, const T& payload) {
    struct msghdr msg{};
    struct iovec iov[2];

    iov[0].iov_base = const_cast<MsgHeader*>(&hdr);
    iov[0].iov_len = sizeof(hdr);

    iov[1].iov_base = const_cast<T*>(&payload);
    iov[1].iov_len = sizeof(payload);

    msg.msg_iov = iov;
    msg.msg_iovlen = 2;

    union {
        struct cmsghdr cm;
        char control[CMSG_SPACE(sizeof(int))];
    } control_un;

    msg.msg_control = control_un.control;
    msg.msg_controllen = sizeof(control_un.control);

    struct cmsghdr* cmptr = CMSG_FIRSTHDR(&msg);
    cmptr->cmsg_len = CMSG_LEN(sizeof(int));
    cmptr->cmsg_level = SOL_SOCKET;
    cmptr->cmsg_type = SCM_RIGHTS;
    *reinterpret_cast<int*>(CMSG_DATA(cmptr)) = fd_to_send;

    return ::sendmsg(sock, &msg, 0);
}

} // namespace

AppWindow::AppWindow(const std::string& title, uint32_t width, uint32_t height)
    : title_(title), width_(width), height_(height) {
    stride_ = width_ * 4;
}

AppWindow::~AppWindow() {
    cleanup();
}

void AppWindow::cleanup() {
    if (pixels_ && shm_capacity_ > 0) {
        ::munmap(pixels_, shm_capacity_);
        pixels_ = nullptr;
    }
    if (shm_fd_ >= 0) {
        ::close(shm_fd_);
        shm_fd_ = -1;
    }
}

bool AppWindow::init_shm(int sock, uint32_t win_id) {
    window_id_ = win_id;
    stride_ = width_ * 4;
    shm_capacity_ = 2560 * 1600 * 4; // Bezpieczna pula 16 MB na dynamiczny resize

    std::string shm_name = "/aqua_shm_" + std::to_string(::getpid()) + "_" + std::to_string(win_id);
    shm_fd_ = ::shm_open(shm_name.c_str(), O_RDWR | O_CREAT | O_EXCL, 0600);
    if (shm_fd_ >= 0) {
        ::shm_unlink(shm_name.c_str());
    } else {
        char tmp_template[] = "/tmp/aqua_shm_XXXXXX";
        shm_fd_ = ::mkstemp(tmp_template);
        if (shm_fd_ >= 0) ::unlink(tmp_template);
    }

    if (shm_fd_ < 0) {
        std::cerr << "[libaqua BŁĄD] Nie mozna utworzyc bufora SHM dla okna " << win_id << std::endl;
        return false;
    }

    if (::ftruncate(shm_fd_, shm_capacity_) < 0) {
        std::cerr << "[libaqua BŁĄD] ftruncate zakonczone niepowodzeniem" << std::endl;
        ::close(shm_fd_);
        shm_fd_ = -1;
        return false;
    }

    pixels_ = static_cast<uint8_t*>(::mmap(nullptr, shm_capacity_, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd_, 0));
    if (pixels_ == MAP_FAILED) {
        std::cerr << "[libaqua BŁĄD] mmap zakonczone niepowodzeniem" << std::endl;
        ::close(shm_fd_);
        shm_fd_ = -1;
        pixels_ = nullptr;
        return false;
    }

    // Przekaż deskryptor SHM do WindowServera
    MsgAttachShm attach_req{width_, height_, stride_, 0};
    MsgHeader attach_hdr{MessageType::AttachShm, sizeof(attach_req), 0, window_id_};

    if (send_fd_with_payload(sock, shm_fd_, attach_hdr, attach_req) < 0) {
        std::cerr << "[libaqua BŁĄD] Blad wysylania deskryptora SHM przez SCM_RIGHTS" << std::endl;
        cleanup();
        return false;
    }

    needs_redraw_ = true;
    on_create();
    return true;
}

void AppWindow::handle_resize(int sock, uint32_t new_w, uint32_t new_h) {
    if (new_w == 0 || new_h == 0) return;
    if (new_w == width_ && new_h == height_) return;

    width_ = std::min(2560u, new_w);
    height_ = std::min(1600u, new_h);
    stride_ = width_ * 4;

    // Aktualizuj geometrię w WindowServerze
    MsgAttachShm attach_req{width_, height_, stride_, 0};
    MsgHeader attach_hdr{MessageType::AttachShm, sizeof(attach_req), 0, window_id_};
    send_fd_with_payload(sock, shm_fd_, attach_hdr, attach_req);

    needs_redraw_ = true;
    on_resize(width_, height_);
}

void AppWindow::render_and_commit(int sock) {
    if (!pixels_ || !needs_redraw_) return;

    Canvas canvas(pixels_, width_, height_, stride_);
    on_draw(canvas);

    MsgHeader commit_hdr{MessageType::CommitBuffer, 0, 0, window_id_};
    ::send(sock, &commit_hdr, sizeof(commit_hdr), 0);

    needs_redraw_ = false;
}

} // namespace aqua
