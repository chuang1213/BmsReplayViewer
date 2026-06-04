cmake_minimum_required(VERSION 3.20)
project(bmv LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- FetchContent (URL-based to avoid git firewall issues) ---
include(FetchContent)

# GLFW 3.4 — download release archive via HTTPS (ZIP, ~1.5 MB)
# If GitHub is unreachable, swap the URL to a mirror:
#   https://kkgithub.com/glfw/glfw/archive/refs/tags/3.4.zip
FetchContent_Declare(
    glfw
    URL https://github.com/glfw/glfw/archive/refs/tags/3.4.zip
    TIMEOUT 30
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(glfw)

# Dear ImGui (docking branch) — download archive via HTTPS (ZIP, ~5 MB)
# Mirror: https://kkgithub.com/ocornut/imgui/archive/refs/heads/docking.zip
FetchContent_Declare(
    imgui
    URL https://github.com/ocornut/imgui/archive/refs/heads/docking.zip
    TIMEOUT 30
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(imgui)

# --- Executable ---
add_executable(bmv
    src/main.cpp
    src/core/timeline.cpp
    src/core/time_map.cpp
    src/format/bms_parser.cpp
    src/render/png_renderer.cpp
    src/replay/brd_parser.cpp
    src/replay/lr2rep_parser.cpp
    src/replay/lr2_random.cpp
    src/replay/replay_adapter.cpp
    src/replay/replay_factory.cpp
    src/app/application.cpp
    src/app/panels/chart_view.cpp
    src/app/panels/video_export.cpp
    src/app/panels/welcome_panel.cpp
    src/app/panels/about_panel.cpp
    src/analysis/judgement_engine.cpp
    src/judge/judge_profile.cpp
    libs/miniz/miniz_tinfl.c

    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)

target_include_directories(bmv PRIVATE
    src
    libs/stb
    libs/miniz
    libs/nlohmann
    libs/picosha2
    libs/md5
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends
)

target_link_libraries(bmv PRIVATE glfw)
if(WIN32)
    target_link_libraries(bmv PRIVATE opengl32)
endif()

# Debug logging control for library code
option(BMV_DEBUG "Enable debug logging in library code" OFF)
if(BMV_DEBUG)
    target_compile_definitions(bmv PRIVATE BMV_DEBUG)
endif()

if(MSVC)
    target_compile_options(bmv PRIVATE /W4 /utf-8)
    target_compile_definitions(bmv PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(bmv PRIVATE -Wall -Wextra)
endif()
