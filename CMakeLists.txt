cmake_minimum_required(VERSION 3.20)
project(VideoLab LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Interfaz MFC (DLL compartida). Requiere el componente "C++ MFC" de Visual Studio.
set(CMAKE_MFC_FLAG 2)

# FFmpeg viene de vcpkg (ver vcpkg.json)
find_package(FFMPEG REQUIRED)

add_executable(VideoLab WIN32
    src/main.cpp
    src/Widgets.cpp       src/Widgets.h
    src/Scopes.cpp        src/Scopes.h
    src/MediaSource.cpp   src/MediaSource.h
    src/Renderer.cpp      src/Renderer.h
    src/Exporter.cpp      src/Exporter.h
    src/AudioSource.cpp   src/AudioSource.h
    src/AudioMix.cpp      src/AudioMix.h
    src/AudioEngine.cpp   src/AudioEngine.h
    src/Project.h src/Theme.h src/Util.h)

# swresample (conversion de audio): lo trae vcpkg junto con FFmpeg
find_library(SWRESAMPLE_LIBRARY NAMES swresample REQUIRED)

target_include_directories(VideoLab PRIVATE ${FFMPEG_INCLUDE_DIRS})
target_link_directories(VideoLab PRIVATE ${FFMPEG_LIBRARY_DIRS})
target_link_libraries(VideoLab PRIVATE ${FFMPEG_LIBRARIES} ${SWRESAMPLE_LIBRARY} winmm user32 gdi32 comdlg32 comctl32 shell32 dwmapi)

target_compile_definitions(VideoLab PRIVATE UNICODE _UNICODE)

if(MSVC)
    target_compile_options(VideoLab PRIVATE /W3 /utf-8)
    target_compile_definitions(VideoLab PRIVATE _CRT_SECURE_NO_WARNINGS NOMINMAX)
endif()
