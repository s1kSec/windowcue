cmake_minimum_required(VERSION 3.20)
project(WindowCueNative LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_executable(WindowCueNative WIN32
    src/Main.cpp
    src/Platform.cpp
    src/SvgIconRenderer.cpp
    WindowCueNative.rc)

target_compile_definitions(WindowCueNative PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _WIN32_WINNT=0x0A00)

if(WINDOWCUE_LANG STREQUAL "EN")
    target_compile_definitions(WindowCueNative PRIVATE LANG_EN)
endif()

if(MSVC)
    target_compile_options(WindowCueNative PRIVATE /W4 /permissive- /utf-8 /EHsc)
    target_link_options(WindowCueNative PRIVATE /SUBSYSTEM:WINDOWS,6.02)
endif()

target_link_libraries(WindowCueNative PRIVATE
    user32 gdi32 shell32 shlwapi ole32 dwmapi d2d1 d3d11 dxgi msimg32 uxtheme
    advapi32 runtimeobject windowsapp)
