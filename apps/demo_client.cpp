#include "aqua_protocol.h"
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/mman.h>
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

template <typename T>
int send_fd_with_payload(int sock, int fd_to_send, const aqua::MsgHeader& hdr, const T& payload) {
    struct msghdr msg{};
    struct iovec iov[2];

    iov[0].iov_base = const_cast<aqua::MsgHeader*>(&hdr);
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

int main(int argc, char* argv[]) {
    bool use_dmabuf = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--dmabuf") == 0) {
            use_dmabuf = true;
        }
    }

    std::cout << "========================================================\n"
              << "  Aqua OS - Niezalezny Klient Aplikacji IPC\n"
              << "  Tryb bufora: " << (use_dmabuf ? "dma-buf (GBM GPU)" : "Shared Memory (POSIX SHM)") << "\n"
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
        ::close(sock);
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
    std::strncpy(req_create.title, use_dmabuf ? "Aqua Client (dma-buf)" : "Aqua Client (POSIX SHM)", sizeof(req_create.title) - 1);

    aqua::MsgHeader hdr_create{aqua::MessageType::CreateWindow, sizeof(req_create), 0, 0};
    ::send(sock, &hdr_create, sizeof(hdr_create), 0);
    ::send(sock, &req_create, sizeof(req_create), 0);

    // Oczekiwanie na potwierdzenie utworzenia okna
    aqua::MsgHeader resp{};
    ssize_t resp_len = ::recv(sock, &resp, sizeof(resp), 0);
    if (resp_len < static_cast<ssize_t>(sizeof(resp))) {
        std::cerr << "[Klient] Blad odbierania potwierdzenia utworzenia okna!" << std::endl;
        ::close(sock);
        return 1;
    }
    std::cout << "[Klient] Okno utworzone pomyslnie! Window ID: " << resp.window_id << std::endl;

    auto start = std::chrono::steady_clock::now();

    if (use_dmabuf) {
        // --- TRYB DMA-BUF (GBM) ---
        int render_fd = ::open("/dev/dri/renderD128", O_RDWR | O_CLOEXEC);
        if (render_fd < 0) {
            render_fd = ::open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
        }
        if (render_fd < 0) {
            std::cerr << "Nie mozna otworzyc urzadzenia DRI: " << std::strerror(errno) << std::endl;
            ::close(sock);
            return 1;
        }

        struct gbm_device* gbm = gbm_create_device(render_fd);
        if (!gbm) {
            std::cerr << "Nie mozna utworzyc urzadzenia GBM!" << std::endl;
            ::close(render_fd);
            ::close(sock);
            return 1;
        }

        struct gbm_bo* bo = gbm_bo_create(gbm, win_w, win_h, GBM_FORMAT_ARGB8888, GBM_BO_USE_RENDERING | GBM_BO_USE_LINEAR);
        if (!bo) {
            std::cerr << "Nie mozna utworzyc bufora GBM BO!" << std::endl;
            gbm_device_destroy(gbm);
            ::close(render_fd);
            ::close(sock);
            return 1;
        }

        int dmabuf_fd = gbm_bo_get_fd(bo);
        uint32_t stride = gbm_bo_get_stride(bo);

        std::cout << "[Klient dma-buf] Zaalokowano bufor GPU w GBM: " << win_w << "x" << win_h 
                  << ", stride: " << stride << ", dma-buf fd: " << dmabuf_fd << std::endl;

        aqua::MsgAttachDmaBuf attach_req{win_w, win_h, stride, DRM_FORMAT_ARGB8888};
        aqua::MsgHeader attach_hdr{aqua::MessageType::AttachDmaBuf, sizeof(attach_req), 0, resp.window_id};

        if (send_fd_with_payload(sock, dmabuf_fd, attach_hdr, attach_req) < 0) {
            std::cerr << "Blad wysylania dma-buf przez SCM_RIGHTS: " << std::strerror(errno) << std::endl;
            ::close(dmabuf_fd);
            gbm_bo_destroy(bo);
            gbm_device_destroy(gbm);
            ::close(render_fd);
            ::close(sock);
            return 1;
        }
        ::close(dmabuf_fd);

        std::vector<uint32_t> local_buffer(win_w * win_h);

        while (true) {
            auto now = std::chrono::steady_clock::now();
            float t = std::chrono::duration<float>(now - start).count();

            for (uint32_t y = 0; y < win_h; ++y) {
                for (uint32_t x = 0; x < win_w; ++x) {
                    float u = static_cast<float>(x) / win_w;
                    float v = static_cast<float>(y) / win_h;

                    float dist = std::hypot(u - 0.5f, v - 0.5f);
                    float wave = std::sin(dist * 18.0f - t * 3.5f) * 0.5f + 0.5f;

                    uint8_t r = static_cast<uint8_t>((std::sin(t * 1.2f + u * 2.5f) * 0.5f + 0.5f) * 220);
                    uint8_t g = static_cast<uint8_t>((std::cos(t * 0.8f + v * 2.5f) * 0.5f + 0.5f) * 180 * wave);
                    uint8_t b = static_cast<uint8_t>((0.75f + 0.25f * std::sin(t * 2.0f + dist * 5.0f)) * 255);

                    local_buffer[y * win_w + x] = (0xFF << 24) | (r << 16) | (g << 8) | b;
                }
            }

            uint32_t map_stride = 0;
            void* map_data = nullptr;
            void* map = gbm_bo_map(bo, 0, 0, win_w, win_h, GBM_BO_TRANSFER_WRITE, &map_stride, &map_data);
            if (map) {
                char* dst = static_cast<char*>(map);
                const char* src = reinterpret_cast<const char*>(local_buffer.data());
                for (uint32_t y = 0; y < win_h; ++y) {
                    std::memcpy(dst + y * map_stride, src + y * win_w * 4, win_w * 4);
                }
                gbm_bo_unmap(bo, map_data);
            }

            aqua::MsgHeader commit_hdr{aqua::MessageType::CommitBuffer, 0, 0, resp.window_id};
            if (::send(sock, &commit_hdr, sizeof(commit_hdr), 0) < 0) {
                std::cout << "[Klient] Serwer zakonczyl polaczenie." << std::endl;
                break;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }

        gbm_bo_destroy(bo);
        gbm_device_destroy(gbm);
        ::close(render_fd);
    } else {
        // --- TRYB SHARED MEMORY (POSIX SHM) ---
        const uint32_t stride = win_w * 4;
        const size_t buf_size = static_cast<size_t>(stride) * win_h;

        int shm_fd = -1;
        std::string shm_name = "/aqua_demo_shm_" + std::to_string(::getpid());
        shm_fd = ::shm_open(shm_name.c_str(), O_RDWR | O_CREAT | O_EXCL, 0600);
        if (shm_fd >= 0) {
            ::shm_unlink(shm_name.c_str());
        } else {
            char tmp_template[] = "/tmp/aqua_shm_XXXXXX";
            shm_fd = ::mkstemp(tmp_template);
            if (shm_fd >= 0) ::unlink(tmp_template);
        }

        if (shm_fd < 0) {
            std::cerr << "Nie mozna utworzyc bufora pamieci wspoldzielonej!" << std::endl;
            ::close(sock);
            return 1;
        }

        if (::ftruncate(shm_fd, buf_size) < 0) {
            std::cerr << "ftruncate zakonczone niepowodzeniem!" << std::endl;
            ::close(shm_fd);
            ::close(sock);
            return 1;
        }

        uint8_t* pixels = static_cast<uint8_t*>(::mmap(nullptr, buf_size, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0));
        if (pixels == MAP_FAILED) {
            std::cerr << "mmap bufora SHM zakonczone niepowodzeniem!" << std::endl;
            ::close(shm_fd);
            ::close(sock);
            return 1;
        }

        std::cout << "[Klient SHM] Utworzono bufor wspoldzielony " << win_w << "x" << win_h 
                  << " (" << buf_size << " bajtow), fd: " << shm_fd << std::endl;

        aqua::MsgAttachShm attach_req{win_w, win_h, stride, 0};
        aqua::MsgHeader attach_hdr{aqua::MessageType::AttachShm, sizeof(attach_req), 0, resp.window_id};

        if (send_fd_with_payload(sock, shm_fd, attach_hdr, attach_req) < 0) {
            std::cerr << "Blad wysylania shm_fd przez SCM_RIGHTS: " << std::strerror(errno) << std::endl;
            ::munmap(pixels, buf_size);
            ::close(shm_fd);
            ::close(sock);
            return 1;
        }

        std::cout << "[Klient SHM] Bufor przekazany pomyslnie! Rozpoczynanie renderowania 60 FPS..." << std::endl;

        while (true) {
            auto now = std::chrono::steady_clock::now();
            float t = std::chrono::duration<float>(now - start).count();

            for (uint32_t y = 0; y < win_h; ++y) {
                uint8_t* row = pixels + y * stride;
                for (uint32_t x = 0; x < win_w; ++x) {
                    float u = static_cast<float>(x) / win_w;
                    float v = static_cast<float>(y) / win_h;

                    // Animowane fale i koła w estetyce macOS
                    float dist = std::hypot(u - 0.5f, v - 0.5f);
                    float wave = std::sin(dist * 18.0f - t * 3.5f) * 0.5f + 0.5f;

                    uint8_t r = static_cast<uint8_t>((std::sin(t * 1.2f + u * 2.5f) * 0.5f + 0.5f) * 220);
                    uint8_t g = static_cast<uint8_t>((std::cos(t * 0.8f + v * 2.5f) * 0.5f + 0.5f) * 180 * wave);
                    uint8_t b = static_cast<uint8_t>((0.75f + 0.25f * std::sin(t * 2.0f + dist * 5.0f)) * 255);

                    // Bezpośredni zapis RGBA (sub-millisecond)
                    uint8_t* p = row + x * 4;
                    p[0] = r;
                    p[1] = g;
                    p[2] = b;
                    p[3] = 255;
                }
            }

            aqua::MsgHeader commit_hdr{aqua::MessageType::CommitBuffer, 0, 0, resp.window_id};
            if (::send(sock, &commit_hdr, sizeof(commit_hdr), 0) < 0) {
                std::cout << "[Klient] Serwer zakonczyl polaczenie." << std::endl;
                break;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(16)); // ~60 FPS
        }

        ::munmap(pixels, buf_size);
        ::close(shm_fd);
    }

    std::cout << "[Klient] Zamykanie aplikacji demonstracyjnej." << std::endl;
    ::close(sock);
    return 0;
}
