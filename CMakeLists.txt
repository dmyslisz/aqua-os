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

add_executable(aqua-server
    src/main.cpp
    src/drm_backend.cpp
    src/input_manager.cpp
    src/cursor_renderer.cpp
    src/window.cpp
    src/compositor.cpp
    src/desktop_shell.cpp
    src/font_renderer.cpp
)

target_include_directories(aqua-server PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${DRM_INCLUDE_DIRS}
    ${GBM_INCLUDE_DIRS}
    ${EGL_INCLUDE_DIRS}
    ${GLESV2_INCLUDE_DIRS}
    ${INPUT_INCLUDE_DIRS}
    ${UDEV_INCLUDE_DIRS}
)

target_link_libraries(aqua-server PRIVATE
    ${DRM_LIBRARIES}
    ${GBM_LIBRARIES}
    ${EGL_LIBRARIES}
    ${GLESV2_LIBRARIES}
    ${INPUT_LIBRARIES}
    ${UDEV_LIBRARIES}
)

target_compile_options(aqua-server PRIVATE
    -Wall
    -Wextra
    -Wpedantic
    -O2
)
