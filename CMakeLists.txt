cmake_minimum_required(VERSION 3.20)
project(aqua_window_server CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# FreeBSD porty instalują biblioteki i nagłówki w /usr/local
list(APPEND CMAKE_PREFIX_PATH "/usr/local")
include_directories("/usr/local/include" "/usr/local/include/libdrm")
link_directories("/usr/local/lib")

find_package(PkgConfig REQUIRED)

pkg_check_modules(DRM REQUIRED libdrm)
pkg_check_modules(GBM REQUIRED gbm)
pkg_check_modules(EGL REQUIRED egl)
pkg_check_modules(GLESV2 REQUIRED glesv2)
pkg_check_modules(INPUT REQUIRED libinput)
pkg_check_modules(UDEV REQUIRED libudev)
pkg_check_modules(FREETYPE REQUIRED freetype2)

add_executable(aqua-server
    src/main.cpp
    src/drm_backend.cpp
    src/input_manager.cpp
    src/cursor_renderer.cpp
    src/window.cpp
    src/compositor.cpp
    src/desktop_shell.cpp
    src/font_renderer.cpp
    src/status_icons.cpp
    src/ipc_server.cpp
    src/kawase_blur.cpp
)

target_include_directories(aqua-server PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${DRM_INCLUDE_DIRS}
    ${GBM_INCLUDE_DIRS}
    ${EGL_INCLUDE_DIRS}
    ${GLESV2_INCLUDE_DIRS}
    ${INPUT_INCLUDE_DIRS}
    ${UDEV_INCLUDE_DIRS}
    ${FREETYPE_INCLUDE_DIRS}
)

target_link_libraries(aqua-server PRIVATE
    ${DRM_LIBRARIES}
    ${GBM_LIBRARIES}
    ${EGL_LIBRARIES}
    ${GLESV2_LIBRARIES}
    ${INPUT_LIBRARIES}
    ${UDEV_LIBRARIES}
    ${FREETYPE_LIBRARIES}
)

target_compile_options(aqua-server PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -O2
)

# Biblioteka kliencka libaqua (C++20 UI Toolkit dla aplikacji Aqua OS)
add_library(aqua-client STATIC
    src/client/canvas.cpp
    src/client/font.cpp
    src/client/window.cpp
    src/client/application.cpp
)

target_include_directories(aqua-client PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${FREETYPE_INCLUDE_DIRS}
)

target_link_libraries(aqua-client PUBLIC
    ${FREETYPE_LIBRARIES}
)

target_compile_options(aqua-client PRIVATE
    -Wall
    -Wextra
    -O2
)

# Niezależna aplikacja demonstracyjna komunikująca się przez dma-buf / SHM IPC
add_executable(aqua-demo-client
    apps/demo_client.cpp
)

target_include_directories(aqua-demo-client PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${DRM_INCLUDE_DIRS}
    ${GBM_INCLUDE_DIRS}
)

target_link_libraries(aqua-demo-client PRIVATE
    ${DRM_LIBRARIES}
    ${GBM_LIBRARIES}
)

target_compile_options(aqua-demo-client PRIVATE
    -Wall
    -Wextra
    -O2
)

# Natywna aplikacja: Kalkulator macOS (aqua-calc)
add_executable(aqua-calc
    apps/calc.cpp
)

target_link_libraries(aqua-calc PRIVATE
    aqua-client
)

target_compile_options(aqua-calc PRIVATE
    -Wall
    -Wextra
    -O2
)

# Natywna aplikacja: Monitor systemu i specyfikacji sprzętowej (aqua-sysmon)
add_executable(aqua-sysmon
    apps/sysmon.cpp
)

target_link_libraries(aqua-sysmon PRIVATE
    aqua-client
)

target_compile_options(aqua-sysmon PRIVATE
    -Wall
    -Wextra
    -O2
)

