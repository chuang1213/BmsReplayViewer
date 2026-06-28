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
    src/replay/java_random.cpp
    src/replay/lr2rep_parser.cpp
    src/replay/lr2_random.cpp
    src/replay/replay_adapter.cpp
    src/replay/replay_factory.cpp
    src/replay/unified/parser.cpp
    src/replay/unified/brd_parser.cpp
    src/replay/unified/lr2_parser.cpp
    src/replay/unified/random.cpp
    src/replay/unified/adapter.cpp
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

# --- 嵌入 0xProto 字体: 用 xxd -i 把 .ttf 转 C++ 数组 ---
# 输出 src/assets/0xproto_font.h, 由 #include 进 application.cpp。
# 用 -n bmv_proto_font_source 固定变量名, 避免传入绝对路径导致 xxd
# 把盘符/路径信息编入变量名。
add_custom_command(
    OUTPUT  ${CMAKE_CURRENT_SOURCE_DIR}/src/assets/0xproto_font.h
    COMMAND xxd -i -n bmv_proto_font_source
                ${CMAKE_CURRENT_SOURCE_DIR}/0xProtoNerdFontPropo-Regular.ttf
            > ${CMAKE_CURRENT_SOURCE_DIR}/src/assets/0xproto_font.h
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/0xProtoNerdFontPropo-Regular.ttf
    COMMENT "Converting 0xProto font to C++ header"
)
add_custom_target(generate_font
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/assets/0xproto_font.h
)
add_dependencies(bmv generate_font)

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

# --- Test executable for BRD random algorithm ---
add_executable(test_brd_random
    test_brd_random.cpp
    src/replay/java_random.cpp
)

target_include_directories(test_brd_random PRIVATE src)

if(MSVC)
    target_compile_options(test_brd_random PRIVATE /W4 /utf-8)
    target_compile_definitions(test_brd_random PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(test_brd_random PRIVATE -Wall -Wextra)
endif()

# --- Test executable for unified random interface ---
add_executable(test_unified_random
    test_unified_random.cpp
    src/replay/unified/random.cpp
    src/replay/java_random.cpp
    src/replay/lr2_random.cpp
)

target_include_directories(test_unified_random PRIVATE src)

if(MSVC)
    target_compile_options(test_unified_random PRIVATE /W4 /utf-8)
    target_compile_definitions(test_unified_random PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(test_unified_random PRIVATE -Wall -Wextra)
endif()

# --- Test executable for unified BRD parser ---
add_executable(test_brd_parser
    test_brd_parser.cpp
    src/replay/unified/random.cpp
    src/replay/unified/brd_parser.cpp
    src/replay/java_random.cpp
    src/replay/lr2_random.cpp
    libs/miniz/miniz_tinfl.c
)

target_include_directories(test_brd_parser PRIVATE
    src
    libs/miniz
    libs/nlohmann
    libs/picosha2
    libs/md5
)

if(MSVC)
    target_compile_options(test_brd_parser PRIVATE /W4 /utf-8)
    target_compile_definitions(test_brd_parser PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(test_brd_parser PRIVATE -Wall -Wextra)
endif()

# --- Test executable for unified LR2 parser ---
add_executable(test_lr2_parser
    test_lr2_parser.cpp
    src/replay/unified/random.cpp
    src/replay/unified/lr2_parser.cpp
    src/replay/lr2_random.cpp
    src/replay/java_random.cpp
    src/replay/lr2rep_parser.cpp
    src/replay/replay_adapter.cpp
    src/core/time_map.cpp
)

target_include_directories(test_lr2_parser PRIVATE
    src
    libs/nlohmann
    libs/picosha2
    libs/md5
)

if(MSVC)
    target_compile_options(test_lr2_parser PRIVATE /W4 /utf-8)
    target_compile_definitions(test_lr2_parser PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(test_lr2_parser PRIVATE -Wall -Wextra)
endif()

# --- Test executable for unified parser factory ---
add_executable(test_parser
    test_parser.cpp
    src/replay/unified/parser.cpp
    src/replay/unified/random.cpp
    src/replay/unified/brd_parser.cpp
    src/replay/unified/lr2_parser.cpp
    src/replay/java_random.cpp
    src/replay/lr2_random.cpp
    libs/miniz/miniz_tinfl.c
)

target_include_directories(test_parser PRIVATE
    src
    libs/miniz
    libs/nlohmann
    libs/picosha2
    libs/md5
)

if(MSVC)
    target_compile_options(test_parser PRIVATE /W4 /utf-8)
    target_compile_definitions(test_parser PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(test_parser PRIVATE -Wall -Wextra)
endif()

# --- Test executable for unified → legacy adapter ---
add_executable(test_adapter
    test_adapter.cpp
    src/replay/unified/adapter.cpp
    src/replay/unified/parser.cpp
    src/replay/unified/random.cpp
    src/replay/unified/brd_parser.cpp
    src/replay/unified/lr2_parser.cpp
    src/replay/java_random.cpp
    src/replay/lr2_random.cpp
    src/replay/replay_adapter.cpp
    src/replay/brd_parser.cpp
    src/replay/lr2rep_parser.cpp
    src/core/time_map.cpp
    libs/miniz/miniz_tinfl.c
)

target_include_directories(test_adapter PRIVATE
    src
    libs/miniz
    libs/nlohmann
    libs/picosha2
    libs/md5
)

if(MSVC)
    target_compile_options(test_adapter PRIVATE /W4 /utf-8)
    target_compile_definitions(test_adapter PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(test_adapter PRIVATE -Wall -Wextra)
endif()
