#include "application.h"
#include "format/bms_parser.h"
#include "replay/lr2rep_parser.h"
#include "app/panels/welcome_panel.h"
#include "app/panels/about_panel.h"
#include "json.hpp"
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "GLFW/glfw3.h"
#include <filesystem>
#define GLFW_EXPOSE_NATIVE_WIN32
#include "GLFW/glfw3native.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#include <shlobj.h>
#include <cstdlib>
#include <cstring>
#endif

#ifdef __linux__
#include <cstdlib>
#endif

#include <cstdio>
#include <fstream>
#include <algorithm>
#include <set>

namespace bmv {

static void glfw_error_callback(int error, const char* desc) {
    std::fprintf(stderr, "[GLFW] error %d: %s\n", error, desc);
}

std::string Application::open_file_dialog(const char* filter_pattern,
                                           const char* title) {
#ifdef _WIN32
    OPENFILENAMEA ofn = {};
    char buf[MAX_PATH] = {};
    ofn.lStructSize  = sizeof(ofn);
    ofn.hwndOwner    = glfwGetWin32Window(window_);
    ofn.lpstrFilter  = filter_pattern;
    ofn.lpstrFile    = buf;
    ofn.nMaxFile     = MAX_PATH;
    ofn.lpstrTitle   = title;
    ofn.Flags        = OFN_FILEMUSTEXIST | OFN_HIDEREADONLY |
                       OFN_PATHMUSTEXIST;
    if (GetOpenFileNameA(&ofn)) return std::string(buf);
#elif defined(__linux__)
    char buf[2048];
    std::snprintf(buf, sizeof(buf),
        "zenity --file-selection --title='%s' 2>/dev/null || "
        "kdialog --getopenfilename . 2>/dev/null", title);
    FILE* p = popen(buf, "r");
    if (p) {
        std::string result;
        char tmp[1024];
        while (fgets(tmp, sizeof(tmp), p)) result += tmp;
        pclose(p);
        while (!result.empty() && (result.back() == '\n' || result.back() == '\r'))
            result.pop_back();
        return result;
    }
#endif
    return {};
}

static const char* chart_filter =
    "BMS Files (*.bms;*.bme;*.bml;*.bmson)\0*.bms;*.bme;*.bml;*.bmson\0"
    "All Files (*.*)\0*.*\0";

static const char* replay_filter =
    "Replay Files (*.brd;*.lr2rep)\0*.brd;*.lr2rep\0"
    "All Files (*.*)\0*.*\0";

void Application::load_chart_file(const std::string& path) {
    BmsParser parser;
    RawChartData raw = parser.parse(path.c_str());
    if (raw.channels.empty()) {
        std::fprintf(stderr, "[Application] no data in %s\n", path.c_str());
        return;
    }
    timeline_ = build_timeline(raw);
    chart_loaded_ = true;
    active_tab_ = 1;
    pending_tab_ = 1;
    add_recent_chart(path);
    std::printf("[Application] loaded chart: %s (%zu notes, %zu measures)\n",
                path.c_str(), timeline_.notes.size(), timeline_.measures.size());
    reload_chart_view();
}

void Application::load_replay_file(const std::string& path) {
    if (!chart_loaded_) {
        std::fprintf(stderr, "[Application] load chart first\n");
        return;
    }
    auto dot = path.rfind('.');
    if (dot != std::string::npos) {
        std::string ext = path.substr(dot);
        for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (ext == ".lr2rep") {
            Lr2RepParser lr2;
            replay_data_ = lr2.parse(path.c_str(), timeline_.time_map);
        } else {
            BrdParser brd;
            replay_data_ = brd.parse(path.c_str(), timeline_.time_map);
        }
    } else {
        std::fprintf(stderr, "[Application] unsupported replay extension: %s\n", path.c_str());
        return;
    }
    add_recent_replay(path);
    replay_loaded_ = true;
    std::printf("[Application] loaded replay: %s (%zu hits)\n",
                path.c_str(), replay_data_.hits.size());
    reload_chart_view();
}

void Application::reload_chart_view() {
    if (chart_loaded_) {
        chart_view_.set_data(&timeline_,
            replay_loaded_ ? &replay_data_ : nullptr);
    }
}

static std::string get_recent_path() {
    std::filesystem::path dir;
#ifdef _WIN32
    const char* appdata = std::getenv("APPDATA");
    dir = appdata ? std::filesystem::path(appdata) / "BMV"
                  : std::filesystem::current_path();
#else
    const char* home = std::getenv("HOME");
    dir = home ? std::filesystem::path(home) / ".config" / "bmv"
               : std::filesystem::current_path();
#endif
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return (dir / "recent_files.json").string();
}

void Application::load_recent_files() {
    std::ifstream f(get_recent_path());
    if (!f.is_open()) return;
    try {
        auto j = nlohmann::json::parse(f);
        if (j.contains("charts") && j["charts"].is_array()) {
            for (auto& e : j["charts"])
                if (e.is_string()) recent_.charts.push_back(e.get<std::string>());
        }
        if (j.contains("replays") && j["replays"].is_array()) {
            for (auto& e : j["replays"])
                if (e.is_string()) recent_.replays.push_back(e.get<std::string>());
        }
        if (j.contains("config") && j["config"].is_object()) {
            auto& cfg = j["config"];
            if (cfg.contains("note_thickness"))
                chart_view_.set_note_thickness(cfg["note_thickness"].get<float>());
            if (cfg.contains("scroll_distance"))
                chart_view_.set_scroll_distance(cfg["scroll_distance"].get<int>());
            if (cfg.contains("replay_display_mode"))
                chart_view_.set_replay_display_mode(cfg["replay_display_mode"].get<int>());
            if (cfg.contains("auto_follow_playback"))
                chart_view_.set_auto_follow_playback(cfg["auto_follow_playback"].get<bool>());
        }
    } catch (...) {}
}

void Application::save_recent_files() {
    nlohmann::json j;
    j["charts"]  = recent_.charts;
    j["replays"] = recent_.replays;

    nlohmann::json cfg;
    cfg["note_thickness"]       = chart_view_.note_thickness();
    cfg["scroll_distance"]      = chart_view_.scroll_distance();
    cfg["replay_display_mode"]  = chart_view_.replay_display_mode_int();
    cfg["auto_follow_playback"] = chart_view_.auto_follow_playback();
    j["config"] = cfg;

    std::ofstream f(get_recent_path());
    if (f.is_open()) f << j.dump(2);
}

void Application::add_recent_chart(const std::string& path) {
    auto& v = recent_.charts;
    v.erase(std::remove(v.begin(), v.end(), path), v.end());
    v.insert(v.begin(), path);
    if (static_cast<int>(v.size()) > RecentList::kMax)
        v.resize(RecentList::kMax);
    save_recent_files();
}

void Application::add_recent_replay(const std::string& path) {
    auto& v = recent_.replays;
    v.erase(std::remove(v.begin(), v.end(), path), v.end());
    v.insert(v.begin(), path);
    if (static_cast<int>(v.size()) > RecentList::kMax)
        v.resize(RecentList::kMax);
    save_recent_files();
}

void Application::on_drop(GLFWwindow* window, int count, const char** paths) {
    auto* app = static_cast<Application*>(glfwGetWindowUserPointer(window));
    if (!app) return;

    std::vector<std::string> charts, replays;

    for (int i = 0; i < count; ++i) {
        std::string p(paths[i]);
        auto dot = p.rfind('.');
        if (dot == std::string::npos) continue;
        std::string ext = p.substr(dot);
        for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (ext == ".bms" || ext == ".bme" || ext == ".bml" || ext == ".bmson")
            charts.push_back(p);
        else if (ext == ".brd" || ext == ".lr2rep")
            replays.push_back(p);
    }

    for (auto& c : charts)  app->load_chart_file(c);
    for (auto& r : replays) app->load_replay_file(r);
}
void Application::render_analyzer_tab() {
    if (chart_loaded_) {
        chart_view_.render_analyzer();
    } else {
        ImGui::SetCursorPosY(ImGui::GetContentRegionAvail().y * 0.4f);
        ImGui::TextDisabled("No chart loaded.");
        ImGui::Spacing();
        if (ImGui::Button("Open Chart...", ImVec2(140, 0))) {
            std::string p = open_file_dialog(chart_filter, "Open BMS Chart");
            if (!p.empty()) load_chart_file(p);
        }
        ImGui::SameLine();
        if (ImGui::Button("Open Replay...", ImVec2(140, 0))) {
            std::string p = open_file_dialog(replay_filter, "Open Replay File");
            if (!p.empty()) { if (chart_loaded_) load_replay_file(p); }
        }
    }
}

void Application::render_tab_bar() {
    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoBringToFrontOnFocus;

    ImVec2 vp = ImGui::GetMainViewport()->WorkPos;
    ImVec2 vs = ImGui::GetMainViewport()->WorkSize;
    ImGui::SetNextWindowPos(vp);
    ImGui::SetNextWindowSize(vs);

    ImGui::Begin("##MainWorkspace", nullptr, flags);

    if (ImGui::BeginTabBar("MainTabBar")) {
        auto wf = (pending_tab_ == 0) ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
        if (ImGui::BeginTabItem("Welcome", nullptr, wf)) {
            active_tab_ = 0;
            auto wa = render_welcome_panel();
            if (wa == WelcomeAction::OpenChart) {
                std::string p = open_file_dialog(chart_filter, "Open BMS Chart");
                if (!p.empty()) load_chart_file(p);
            } else if (wa == WelcomeAction::OpenReplay) {
                std::string p = open_file_dialog(replay_filter, "Open Replay File");
                if (!p.empty()) load_replay_file(p);
            }
            ImGui::EndTabItem();
        }

        auto af = (pending_tab_ == 1) ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
        if (ImGui::BeginTabItem("Analyzer", nullptr, af)) {
            active_tab_ = 1;
            render_analyzer_tab();
            ImGui::EndTabItem();
        }

        auto bf = (pending_tab_ == 2) ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
        if (ImGui::BeginTabItem("About", nullptr, bf)) {
            active_tab_ = 2;
            render_about_panel();
            ImGui::EndTabItem();
        }

        pending_tab_ = -1;
        ImGui::EndTabBar();
    }

    ImGui::End();
}

void Application::render_menu_bar() {
    if (!ImGui::BeginMainMenuBar()) return;

    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("Open Chart...")) {
            std::string p = open_file_dialog(chart_filter, "Open BMS Chart");
            if (!p.empty()) load_chart_file(p);
        }
        if (ImGui::MenuItem("Open Replay...")) {
            std::string p = open_file_dialog(replay_filter, "Open Replay File");
            if (!p.empty()) { if (chart_loaded_) load_replay_file(p); }
        }
        ImGui::Separator();

        if (!recent_.charts.empty()) {
            ImGui::TextDisabled("Recent Charts");
            for (size_t i = 0; i < recent_.charts.size(); ++i) {
                const auto& p = recent_.charts[i];
                auto slash = p.rfind('/');
#ifdef _WIN32
                if (slash == std::string::npos) slash = p.rfind('\\');
#endif
                std::string label = (slash != std::string::npos)
                    ? p.substr(slash + 1) : p;
                label = std::to_string(i + 1) + ". " + label;
                if (ImGui::MenuItem(label.c_str())) load_chart_file(p);
            }
            ImGui::Separator();
        }

        if (!recent_.replays.empty()) {
            ImGui::TextDisabled("Recent Replays");
            for (size_t i = 0; i < recent_.replays.size(); ++i) {
                const auto& p = recent_.replays[i];
                auto slash = p.rfind('/');
#ifdef _WIN32
                if (slash == std::string::npos) slash = p.rfind('\\');
#endif
                std::string label = (slash != std::string::npos)
                    ? p.substr(slash + 1) : p;
                label = std::to_string(i + 1) + ". " + label;
                if (ImGui::MenuItem(label.c_str())) load_replay_file(p);
            }
            ImGui::Separator();
        }

        if (ImGui::MenuItem("Export Video...")) show_export_dialog_ = true;
        ImGui::Separator();
        if (ImGui::MenuItem("Exit")) glfwSetWindowShouldClose(window_, true);
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Help")) {
        if (ImGui::MenuItem("Welcome Page")) pending_tab_ = 0;
        if (ImGui::MenuItem("About")) pending_tab_ = 2;
        ImGui::EndMenu();
    }

    ImGui::EndMainMenuBar();
}

int Application::run() {
    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit()) { std::fprintf(stderr, "[App] GLFW init fail\n"); return 1; }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    window_ = glfwCreateWindow(1280, 720, "BMV - BMS Visualizer",
                               nullptr, nullptr);
    if (!window_) { glfwTerminate(); return 1; }
    glfwMakeContextCurrent(window_);
    glfwSwapInterval(1);

    glfwSetWindowUserPointer(window_, this);
    glfwSetDropCallback(window_, on_drop);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;
    ImGui::StyleColorsDark();

    ImGui_ImplGlfw_InitForOpenGL(window_, true);
    ImGui_ImplOpenGL3_Init("#version 150");

    load_recent_files();

    active_tab_ = 0;
    chart_loaded_ = false;

    while (!glfwWindowShouldClose(window_)) {
        glfwPollEvents();

        if (video_exporter_.is_exporting())
            video_exporter_.process_frame();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        render_menu_bar();
        render_tab_bar();

        if (active_tab_ == 1 && chart_loaded_)
            chart_view_.render_controls_window();

        if (show_export_dialog_) {
            ImGui::SetNextWindowSize(ImVec2(360, 320), ImGuiCond_FirstUseEver);
            if (ImGui::Begin("Export Video", &show_export_dialog_)) {
                ImGui::InputInt("Width",  &export_cfg_.width);
                ImGui::InputInt("Height", &export_cfg_.height);
                ImGui::InputInt("FPS",    &export_cfg_.fps);
                ImGui::Checkbox("Green Screen", &export_cfg_.green_screen);
                ImGui::InputText("Output", export_cfg_.output_path, sizeof(export_cfg_.output_path));
                ImGui::Separator();
                ImGui::TextDisabled("Requires ffmpeg in system PATH.");
                if (chart_loaded_) {
                    double ts = timeline_.time_map.tick_to_second(timeline_.tick_end());
                    int tf = static_cast<int>(ts * export_cfg_.fps) + 1;
                    ImGui::Text("Duration: %.1f s  |  Frames: %d", ts, tf);
                }
                if (ImGui::Button("Start Export", ImVec2(-1, 0))) {
                    video_exporter_.start_export(
                        export_cfg_, timeline_,
                        replay_data_.hits.empty() ? nullptr : &replay_data_,
                        chart_view_.bms_lane_to_display(),
                        chart_view_.config.is_2p_layout,
                        chart_view_.config.show_replay,
                        chart_view_.pixels_per_tick());
                    show_export_dialog_ = false;
                }
            }
            ImGui::End();
        }

        video_exporter_.render_ui();

        ImGui::Render();
        int dw, dh;
        glfwGetFramebufferSize(window_, &dw, &dh);
        glViewport(0, 0, dw, dh);
        glClearColor(0.12f, 0.12f, 0.14f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        glfwSwapBuffers(window_);
    }

    save_recent_files();
    video_exporter_.cancel_export();

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    glfwDestroyWindow(window_);
    glfwTerminate();
    return 0;
}

} // namespace bmv
