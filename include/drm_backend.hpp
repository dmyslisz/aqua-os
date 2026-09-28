#pragma once

#include <cstdint>
#include <string>
#include <memory>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <gbm.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl3.h>

namespace aqua {

struct DrmFb {
    uint32_t fb_id{0};
    struct gbm_bo* bo{nullptr};
};

class DrmBackend {
public:
    DrmBackend();
    ~DrmBackend();

    DrmBackend(const DrmBackend&) = delete;
    DrmBackend& operator=(const DrmBackend&) = delete;

    bool initialize(const std::string& card_path = "/dev/dri/card0");
    void shutdown();

    // Wymiana buforów i zgłoszenie page-flip
    bool start_page_flip();
    void process_drm_events();
    bool waiting_for_flip() const { return waiting_for_flip_; }

    uint32_t width() const { return mode_.hdisplay; }
    uint32_t height() const { return mode_.vdisplay; }
    uint32_t refresh_rate() const { return mode_.vrefresh; }

    int drm_fd() const { return drm_fd_; }
    EGLDisplay egl_display() const { return egl_dpy_; }

private:
    bool init_kms();
    bool init_gbm();
    bool init_egl();
    uint32_t get_fb_for_bo(struct gbm_bo* bo);

    int drm_fd_{-1};
    uint32_t connector_id_{0};
    uint32_t crtc_id_{0};
    int crtc_index_{-1};
    drmModeModeInfo mode_{};
    drmModeCrtcPtr orig_crtc_{nullptr};

    // GBM
    struct gbm_device* gbm_dev_{nullptr};
    struct gbm_surface* gbm_surf_{nullptr};
    struct gbm_bo* current_bo_{nullptr};
    struct gbm_bo* next_bo_{nullptr};

    // EGL
    EGLDisplay egl_dpy_{EGL_NO_DISPLAY};
    EGLContext egl_ctx_{EGL_NO_CONTEXT};
    EGLSurface egl_surf_{EGL_NO_SURFACE};

    bool waiting_for_flip_{false};
};

} // namespace aqua
