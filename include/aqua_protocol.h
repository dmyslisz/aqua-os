#pragma once

#include <cstdint>

namespace aqua {

constexpr const char* AQUA_DEFAULT_SOCKET_PATH = "/tmp/aqua-server.sock";

enum class MessageType : uint32_t {
    // Klient -> Serwer
    CreateWindow = 1,
    DestroyWindow = 2,
    AttachDmaBuf = 3,
    CommitBuffer = 4,
    SetTitle = 5,
    SetDraggableRegions = 6,
    AttachShm = 7,

    // Serwer -> Klient
    WindowCreated = 100,
    WindowClosed = 101,
    PointerMotion = 102,
    PointerButton = 103,
    KeyboardKey = 104,
    WindowResized = 105
};

#pragma pack(push, 1)

struct MsgHeader {
    MessageType type;
    uint32_t size;      // Rozmiar payloadu za nagłówkiem
    uint32_t client_id;
    uint32_t window_id;
};

// Payload dla CreateWindow
struct MsgCreateWindow {
    uint32_t width;
    uint32_t height;
    uint32_t flags; // bit 0: FullSizeContent
    char title[64];
};

// Payload dla AttachDmaBuf (deskryptor fd przekazywany jest równolegle przez SCM_RIGHTS)
struct MsgAttachDmaBuf {
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint32_t drm_fourcc;
};

// Payload dla AttachShm (deskryptor fd pamięci współdzielonej przekazywany przez SCM_RIGHTS)
struct MsgAttachShm {
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint32_t format; // 0 = RGBA8888, 1 = BGRA8888
};

// Payload dla SetDraggableRegions
struct DragRect {
    float x;
    float y;
    float width;
    float height;
};

struct MsgSetDraggableRegions {
    uint32_t count;
    DragRect rects[16];
};

// Payload dla zdarzeń wejściowych wysyłanych do klienta
struct MsgInputEvent {
    uint32_t event_type; // 1 = motion, 2 = button, 3 = key
    float x;
    float y;
    uint32_t code;
    uint32_t state;
};

#pragma pack(pop)

} // namespace aqua
