#include "drm_backend.hpp"

#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <iostream>
#include <vector>
#include <cstring>
#include <stdexcept>
#include <drm_fourcc.h>

namespace aqua {

namespace {

void page_flip_handler(int /*fd*/, unsigned int /*sequence*/, unsigned int /*tv_sec*/,
                       unsigned int /*tv_usec*/, void* user_data) {
    auto* flag = static_cast<bool*>(user_data);
    *flag = false;
}

void drm_fb_destroy_callback(struct gbm_bo* /*bo*/, void* data) {
    auto* fb = static_cast<DrmFb*>(data);
    if (fb) {
        delete fb;
    }
}

} // namespace

DrmBackend::DrmBackend() = default;

DrmBackend::~DrmBackend() {
    shutdown();
}

bool DrmBackend::initialize(const std::string& card_path) {
    std::cout << "[Aqua KMS] Otwieranie urzadzenia DRM: " << card_path << "..." << std::endl;
    drm_fd_ = open(card_path.c_str(), O_RDWR | O_CLOEXEC);
    if (drm_fd_ < 0) {
        std::cerr << "[Aqua KMS BŁĄD] Nie udalo sie otworzyc " << card_path 
                  << ": " << std::strerror(errno) << std::endl;
        return false;
    }

    // Sprawdzenie możliwości DRM (Universal Planes, itp.)
    uint64_t cap = 0;
    if (drmGetCap(drm_fd_, DRM_CAP_DUMB_BUFFER, &cap) < 0 || !cap) {
        std::cerr << "[Aqua KMS OSTRZEŻENIE] Karta nie obsluguje dumb buffers." << std::endl;
    }

    if (!init_kms()) return false;
    if (!init_gbm()) return false;
    if (!init_egl()) return false;

    std::cout << "[Aqua KMS] Pomyślnie zainicjalizowano silnik graficzny: "
              << mode_.hdisplay << "x" << mode_.vdisplay << " @" << mode_.vrefresh << "Hz" 
              << std::endl;
    return true;
}

bool DrmBackend::init_kms() {
    auto* res = drmModeGetResources(drm_fd_);
    if (!res) {
        std::cerr << "[Aqua KMS BŁĄD] Nie udalo sie pobrac zasobow DRM." << std::endl;
        return false;
    }

    drmModeConnectorPtr conn = nullptr;
    // Znajdź podłączone złącze (preferowane eDP dla laptopa HP 15s)
    for (int i = 0; i < res->count_connectors; ++i) {
        conn = drmModeGetConnector(drm_fd_, res->connectors[i]);
        if (conn) {
            if (conn->connection == DRM_MODE_CONNECTED && conn->count_modes > 0) {
                std::cout << "[Aqua KMS] Znaleziono aktywne zlacze ID: " << conn->connector_id 
                          << " (Typ: " << conn->connector_type << ")" << std::endl;
                break;
            }
            drmModeFreeConnector(conn);
            conn = nullptr;
        }
    }

    if (!conn) {
        std::cerr << "[Aqua KMS BŁĄD] Brak podlaczonych wyswietlaczy!" << std::endl;
        drmModeFreeResources(res);
        return false;
    }

    connector_id_ = conn->connector_id;
    // Wybierz natywny/pierwszy tryb (zazwyczaj preferowany eDP 1080p)
    mode_ = conn->modes[0];
    for (int i = 0; i < conn->count_modes; ++i) {
        if (conn->modes[i].type & DRM_MODE_TYPE_PREFERRED) {
            mode_ = conn->modes[i];
            break;
        }
    }

    // Znajdź odpowiedni encoder i CRTC
    drmModeEncoderPtr enc = nullptr;
    if (conn->encoder_id) {
        enc = drmModeGetEncoder(drm_fd_, conn->encoder_id);
    }

    if (enc) {
        crtc_id_ = enc->crtc_id;
        drmModeFreeEncoder(enc);
    } else {
        // Fallback: przypisz pierwszy pasujący CRTC
        for (int i = 0; i < conn->count_encoders; ++i) {
            enc = drmModeGetEncoder(drm_fd_, conn->encoders[i]);
            if (!enc) continue;
            for (int j = 0; j < res->count_crtcs; ++j) {
                if (enc->possible_crtcs & (1 << j)) {
                    crtc_id_ = res->crtcs[j];
                    crtc_index_ = j;
                    break;
                }
            }
            drmModeFreeEncoder(enc);
            if (crtc_id_ > 0) break;
        }
    }

    drmModeFreeConnector(conn);
    drmModeFreeResources(res);

    if (crtc_id_ == 0) {
        std::cerr << "[Aqua KMS BŁĄD] Nie udalo sie znalezc pasujacego CRTC!" << std::endl;
        return false;
    }

    // Zapisz oryginalny stan CRTC, aby przywrócić go po wyłączeniu kompozytora
    orig_crtc_ = drmModeGetCrtc(drm_fd_, crtc_id_);
    return true;
}

bool DrmBackend::init_gbm() {
    gbm_dev_ = gbm_create_device(drm_fd_);
    if (!gbm_dev_) {
        std::cerr << "[Aqua GBM BŁĄD] gbm_create_device zakonczone niepowodzeniem!" << std::endl;
        return false;
    }

    gbm_surf_ = gbm_surface_create(
        gbm_dev_,
        mode_.hdisplay,
        mode_.vdisplay,
        GBM_FORMAT_XRGB8888,
        GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING
    );

    if (!gbm_surf_) {
        std::cerr << "[Aqua GBM BŁĄD] gbm_surface_create zakonczone niepowodzeniem!" << std::endl;
        return false;
    }

    return true;
}

bool DrmBackend::init_egl() {
    // Inicjalizacja EGL na platformie GBM
    PFNEGLGETPLATFORMDISPLAYEXTPROC eglGetPlatformDisplayEXT =
        reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(eglGetProcAddress("eglGetPlatformDisplayEXT"));

    if (eglGetPlatformDisplayEXT) {
        egl_dpy_ = eglGetPlatformDisplayEXT(EGL_PLATFORM_GBM_KHR, gbm_dev_, nullptr);
    } else {
        egl_dpy_ = eglGetDisplay(reinterpret_cast<EGLNativeDisplayType>(gbm_dev_));
    }

    if (egl_dpy_ == EGL_NO_DISPLAY) {
        std::cerr << "[Aqua EGL BŁĄD] Nie mozna uzyskac EGLDisplay." << std::endl;
        return false;
    }

    EGLint major = 0, minor = 0;
    if (!eglInitialize(egl_dpy_, &major, &minor)) {
        std::cerr << "[Aqua EGL BŁĄD] eglInitialize zakonczone niepowodzeniem." << std::endl;
        return false;
    }

    std::cout << "[Aqua EGL] Zainicjalizowano EGL " << major << "." << minor << std::endl;

    eglBindAPI(EGL_OPENGL_ES_API);

    EGLint num_configs = 0;
    if (!eglGetConfigs(egl_dpy_, nullptr, 0, &num_configs) || num_configs == 0) {
        std::cerr << "[Aqua EGL BŁĄD] Nie udalo sie pobrac liczby konfiguracji EGL." << std::endl;
        return false;
    }

    std::vector<EGLConfig> configs(num_configs);
    if (!eglGetConfigs(egl_dpy_, configs.data(), num_configs, &num_configs)) {
        std::cerr << "[Aqua EGL BŁĄD] Nie udalo sie pobrac konfiguracji EGL." << std::endl;
        return false;
    }

    EGLConfig matched_config = nullptr;
    for (const auto& cfg : configs) {
        EGLint visual_id = 0;
        if (eglGetConfigAttrib(egl_dpy_, cfg, EGL_NATIVE_VISUAL_ID, &visual_id)) {
            if (visual_id == GBM_FORMAT_XRGB8888 || visual_id == GBM_FORMAT_ARGB8888) {
                matched_config = cfg;
                break;
            }
        }
    }

    if (!matched_config) {
        // Fallback do pierwszej konfiguracji
        matched_config = configs[0];
    }

    const EGLint context_attribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 3,
        EGL_NONE
    };

    egl_ctx_ = eglCreateContext(egl_dpy_, matched_config, EGL_NO_CONTEXT, context_attribs);
    if (egl_ctx_ == EGL_NO_CONTEXT) {
        std::cerr << "[Aqua EGL BŁĄD] eglCreateContext zakonczone niepowodzeniem: 0x" 
                  << std::hex << eglGetError() << std::dec << std::endl;
        return false;
    }

    PFNEGLCREATEPLATFORMWINDOWSURFACEEXTPROC eglCreatePlatformWindowSurfaceEXT =
        reinterpret_cast<PFNEGLCREATEPLATFORMWINDOWSURFACEEXTPROC>(eglGetProcAddress("eglCreatePlatformWindowSurfaceEXT"));

    if (eglCreatePlatformWindowSurfaceEXT) {
        egl_surf_ = eglCreatePlatformWindowSurfaceEXT(egl_dpy_, matched_config, gbm_surf_, nullptr);
    } else {
        egl_surf_ = eglCreateWindowSurface(egl_dpy_, matched_config, reinterpret_cast<EGLNativeWindowType>(gbm_surf_), nullptr);
    }

    if (egl_surf_ == EGL_NO_SURFACE) {
        std::cerr << "[Aqua EGL BŁĄD] eglCreateWindowSurface zakonczone niepowodzeniem: 0x" 
                  << std::hex << eglGetError() << std::dec << std::endl;
        return false;
    }

    if (!eglMakeCurrent(egl_dpy_, egl_surf_, egl_surf_, egl_ctx_)) {
        std::cerr << "[Aqua EGL BŁĄD] eglMakeCurrent zakonczone niepowodzeniem." << std::endl;
        return false;
    }

    return true;
}

uint32_t DrmBackend::get_fb_for_bo(struct gbm_bo* bo) {
    auto* fb = static_cast<DrmFb*>(gbm_bo_get_user_data(bo));
    if (fb) {
        return fb->fb_id;
    }

    fb = new DrmFb();
    fb->bo = bo;

    uint32_t width = gbm_bo_get_width(bo);
    uint32_t height = gbm_bo_get_height(bo);
    uint32_t stride = gbm_bo_get_stride(bo);
    uint32_t handle = gbm_bo_get_handle(bo).u32;

    int ret = drmModeAddFB(drm_fd_, width, height, 24, 32, stride, handle, &fb->fb_id);
    if (ret != 0) {
        std::cerr << "[Aqua KMS BŁĄD] drmModeAddFB zakonczone kodem: " << ret << std::endl;
        delete fb;
        return 0;
    }

    gbm_bo_set_user_data(bo, fb, drm_fb_destroy_callback);
    return fb->fb_id;
}

bool DrmBackend::swap_and_page_flip() {
    // 1. Zrzut bufora GL do gbm_surface
    eglSwapBuffers(egl_dpy_, egl_surf_);

    // 2. Pobierz front buffer
    next_bo_ = gbm_surface_lock_front_buffer(gbm_surf_);
    if (!next_bo_) {
        std::cerr << "[Aqua GBM BŁĄD] lock_front_buffer zwrocil nullptr!" << std::endl;
        return false;
    }

    uint32_t fb_id = get_fb_for_bo(next_bo_);
    if (fb_id == 0) return false;

    // Pierwsza klatka - uruchomienie trybu KMS (Modeset)
    if (!current_bo_) {
        int ret = drmModeSetCrtc(drm_fd_, crtc_id_, fb_id, 0, 0, &connector_id_, 1, &mode_);
        if (ret != 0) {
            std::cerr << "[Aqua KMS BŁĄD] drmModeSetCrtc nie powiodl sie: " << ret << std::endl;
            return false;
        }
        current_bo_ = next_bo_;
        return true;
    }

    // Kolejne klatki - asynchroniczny sprzętowy Page-Flip z synchronizacją VSync
    waiting_for_flip_ = true;
    int ret = drmModePageFlip(drm_fd_, crtc_id_, fb_id, DRM_MODE_PAGE_FLIP_EVENT, &waiting_for_flip_);
    if (ret != 0) {
        std::cerr << "[Aqua KMS BŁĄD] drmModePageFlip zakonczony błędem: " << ret << std::endl;
        gbm_surface_release_buffer(gbm_surf_, next_bo_);
        return false;
    }

    // Oczekiwanie na przerwanie VSync od GPU Intela (zero tearingu)
    drmEventContext evctx{};
    evctx.version = 2;
    evctx.page_flip_handler = page_flip_handler;

    struct pollfd pfd{};
    pfd.fd = drm_fd_;
    pfd.events = POLLIN;

    while (waiting_for_flip_) {
        int p = poll(&pfd, 1, -1);
        if (p < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (pfd.revents & POLLIN) {
            drmHandleEvent(drm_fd_, &evctx);
        }
    }

    // Zwolnij poprzedni bufor
    if (current_bo_) {
        gbm_surface_release_buffer(gbm_surf_, current_bo_);
    }
    current_bo_ = next_bo_;

    return true;
}

void DrmBackend::shutdown() {
    if (egl_dpy_ != EGL_NO_DISPLAY) {
        eglMakeCurrent(egl_dpy_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        if (egl_surf_ != EGL_NO_SURFACE) eglDestroySurface(egl_dpy_, egl_surf_);
        if (egl_ctx_ != EGL_NO_CONTEXT) eglDestroyContext(egl_dpy_, egl_ctx_);
        eglTerminate(egl_dpy_);
        egl_dpy_ = EGL_NO_DISPLAY;
    }

    if (current_bo_ && gbm_surf_) {
        gbm_surface_release_buffer(gbm_surf_, current_bo_);
        current_bo_ = nullptr;
    }

    if (gbm_surf_) {
        gbm_surface_destroy(gbm_surf_);
        gbm_surf_ = nullptr;
    }

    if (gbm_dev_) {
        gbm_device_destroy(gbm_dev_);
        gbm_dev_ = nullptr;
    }

    // Przywrócenie pierwotnego stanu CRTC
    if (orig_crtc_ && drm_fd_ >= 0) {
        drmModeSetCrtc(drm_fd_, orig_crtc_->crtc_id, orig_crtc_->buffer_id,
                       orig_crtc_->x, orig_crtc_->y,
                       &connector_id_, 1, &orig_crtc_->mode);
        drmModeFreeCrtc(orig_crtc_);
        orig_crtc_ = nullptr;
    }

    if (drm_fd_ >= 0) {
        close(drm_fd_);
        drm_fd_ = -1;
    }
}

} // namespace aqua
