cmake_minimum_required(VERSION 3.22.1)
project(ultraman_fe0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG")
add_library(ppsspp_core SHARED IMPORTED)
set_target_properties(ppsspp_core PROPERTIES
    IMPORTED_LOCATION ${CMAKE_SOURCE_DIR}/../jniLibs/${ANDROID_ABI}/libppsspp_libretro_android.so)
add_library(ultraman_bridge SHARED
    ulog.cpp
    crash_handler.cpp
    native_bridge.cpp
    libretro_host.cpp
    video_renderer.cpp
    audio_engine.cpp
    input_router.cpp)
target_include_directories(ultraman_bridge PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(ultraman_bridge
    ppsspp_core
    android
    log
    EGL
    GLESv3
    OpenSLES)
target_compile_options(ultraman_bridge PRIVATE -Wall -Wextra -fvisibility=hidden)
