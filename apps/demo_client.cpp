#include "aqua_protocol.h"
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <fcntl.h>
#include <iostream>
#include <cstring>
#include <vector>
#include <cmath>
#include <chrono>
#include <thread>
#include <gbm.h>
#include <xf86drm.h>
#include <drm_fourcc.h>

int send_fd(int sock, int fd_to_send, const aqua::MsgHeader& hdr, const aqua::MsgAttachDmaBuf& payload) {
    struct msghdr msg{};
    struct iovec iov[2];

    iov[0].iov_base = const_cast<aqua::MsgHeader*>(&hdr);
    iov[0].iov_len = sizeof(hdr);

    iov[1].iov_base = const_cast<aqua::MsgAttachDmaBuf*>(&payload);
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

int main() {
    std::cout << "========================================================\n"
              << "  Aqua OS - Niezalezny Klient Aplikacji IPC (dma-buf)\n"
              << "========================================================" << std::endl;

    // 1. Polaczenie z WindowServerem przez gniazdo UNIX
    int sock = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock < 0) {
        std::cerr << "Nie mozna utworzyc gniazda UNIX: " << std::strerror(errno) << std::endl;
        return 1;
    }

    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, aqua::AQUA_DEFAULT_SOCKET_PATH, sizeof(addr.sun_path) - 1);

    if (::connect(sock, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << "Nie mozna polaczyc sie z Aqua WindowServer (" 
                  << aqua::AQUA_DEFAULT_SOCKET_PATH << "): " << std::strerror(errno) << std::endl;
        std::cerr << "Upewnij sie, ze aqua-server jest uruchomiony!" << std::endl;
        return 1;
    }

    std::cout << "[Klient] Polaczono z Aqua WindowServer!" << std::endl;

    // 2. Zadanie utworzenia okna
    const uint32_t win_w = 640;
    const uint32_t win_h = 400;

    aqua::MsgCreateWindow req_create{};
    req_create.width = win_w;
    req_create.height = win_h;
    req_create.flags = 1; // FullSizeContent
    std::strncpy(req_create.title, "Aqua Client (IPC dma-buf)", sizeof(req_create.title) - 1);

    aqua::MsgHeader hdr_create{aqua::MessageType::CreateWindow, sizeof(req_create), 0, 0};
    ::send(sock, &hdr_create, sizeof(hdr_create), 0);
    ::send(sock, &req_create, sizeof(req_create), 0);

    // Oczekiwanie na potwierdzenie utworzenia okna
    aqua::MsgHeader resp{};
    ::recv(sock, &resp, sizeof(resp), 0);
    std::cout << "[Klient] Okno utworzone pomyslnie! Window ID: " << resp.window_id << std::endl;

    // 3. Inicjalizacja GBM i render node GPU (/dev/dri/renderD128)
    int render_fd = ::open("/dev/dri/renderD128", O_RDWR | O_CLOEXEC);
    if (render_fd < 0) {
        render_fd = ::open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
    }
    if (render_fd < 0) {
        std::cerr << "Nie mozna otworzyc urzadzenia DRI: " << std::strerror(errno) << std::endl;
        return 1;
    }

    struct gbm_device* gbm = gbm_create_device(render_fd);
    if (!gbm) {
        std::cerr << "Nie mozna utworzyc urzadzenia GBM!" << std::endl;
        return 1;
    }

    // Alokacja bufora GPU w formacie ARGB8888
    struct gbm_bo* bo = gbm_bo_create(gbm, win_w, win_h, GBM_FORMAT_ARGB8888, GBM_BO_USE_RENDERING | GBM_BO_USE_LINEAR);
    if (!bo) {
        std::cerr << "Nie mozna utworzyc bufora GBM BO!" << std::endl;
        return 1;
    }

    int dmabuf_fd = gbm_bo_get_fd(bo);
    uint32_t stride = gbm_bo_get_stride(bo);

    std::cout << "[Klient] Zaalokowano bufor GPU w GBM: " << win_w << "x" << win_h 
              << ", stride: " << stride << ", dma-buf fd: " << dmabuf_fd << std::endl;

    // 4. Przekazanie deskryptora dma-buf do kompozytora przez SCM_RIGHTS
    aqua::MsgAttachDmaBuf attach_req{win_w, win_h, stride, DRM_FORMAT_ARGB8888};
    aqua::MsgHeader attach_hdr{aqua::MessageType::AttachDmaBuf, sizeof(attach_req), 0, resp.window_id};

    if (send_fd(sock, dmabuf_fd, attach_hdr, attach_req) < 0) {
        std::cerr << "Blad wysylania dma-buf przez SCM_RIGHTS: " << std::strerror(errno) << std::endl;
        return 1;
    }

    std::cout << "[Klient] Bufor dma-buf przekazany do kompozytora! Renderowanie animacji..." << std::endl;

    // 5. Pętla renderowania klienta (animowana zawartość)
    auto start = std::chrono::steady_clock::now();
    for (int frame = 0; frame < 600; ++frame) { // Działa przez ~10 sekund
        auto now = std::chrono::steady_clock::now();
        float t = std::chrono::duration<float>(now - start).count();

        // Mapowanie pamięci bufora GPU i rysowanie dynamicznego gradientu
        uint32_t map_stride = 0;
        void* map_data = nullptr;
        void* map = gbm_bo_map(bo, 0, 0, win_w, win_h, GBM_BO_TRANSFER_WRITE, &map_stride, &map_data);

        if (map) {
            uint32_t* pixels = static_cast<uint32_t*>(map);
            for (uint32_t y = 0; y < win_h; ++y) {
                for (uint32_t x = 0; x < win_w; ++x) {
                    float u = static_cast<float>(x) / win_w;
                    float v = static_cast<float>(y) / win_h;

                    // Dynamiczne koła i fale w stylu Apple
                    float dist = std::hypot(u - 0.5f, v - 0.5f);
                    float ring = std::sin(dist * 20.0f - t * 4.0f) * 0.5f + 0.5f;

                    uint8_t r = static_cast<uint8_t>((std::sin(t + u * 3.0f) * 0.5f + 0.5f) * 255);
                    uint8_t g = static_cast<uint8_t>((std::cos(t + v * 3.0f) * 0.5f + 0.5f) * 200 * ring);
                    uint8_t b = static_cast<uint8_t>((0.8f + 0.2f * std::sin(t * 2.0f)) * 255);

                    pixels[y * (map_stride / 4) + x] = (0xFF << 24) | (r << 16) | (g << 8) | b;
                }
            }
            gbm_bo_unmap(bo, map_data);
        }

        // Zgłoszenie klatki (Commit)
        aqua::MsgHeader commit_hdr{aqua::MessageType::CommitBuffer, 0, 0, resp.window_id};
        ::send(sock, &commit_hdr, sizeof(commit_hdr), 0);

        std::this_thread::sleep_for(std::chrono::milliseconds(16)); // ~60 FPS
    }

    std::cout << "[Klient] Zamykanie aplikacji demonstracyjnej." << std::endl;
    gbm_bo_destroy(bo);
    gbm_device_destroy(gbm);
    ::close(render_fd);
    ::close(sock);
    return 0;
}
