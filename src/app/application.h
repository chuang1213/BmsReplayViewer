#pragma once
#include "core/timeline.h"
#include "replay/replay.h"
#include "app/panels/chart_view.h"
#include "app/panels/video_export.h"
#include <string>
#include <vector>

struct GLFWwindow;

namespace bmv {

class Application {
public:
    Application() = default;
    ~Application() = default;
    int run();

private:
    Timeline      timeline_;
    ReplayData    replay_data_;
    ChartView     chart_view_;
    VideoExporter video_exporter_;
    ExportConfig  export_cfg_;
    bool          chart_loaded_       = false;
    bool          replay_loaded_      = false;
    int           active_tab_         = 0;
    int           pending_tab_        = -1;
    bool          show_export_dialog_ = false;
    GLFWwindow*   window_             = nullptr;

    struct RecentList {
        std::vector<std::string> charts;
        std::vector<std::string> replays;
        static constexpr int kMax = 10;
    };
    RecentList recent_;
    void load_recent_files();
    void save_recent_files();
    void add_recent_chart(const std::string& path);
    void add_recent_replay(const std::string& path);

    void load_chart_file(const std::string& path);
    void load_replay_file(const std::string& path);
    void reload_chart_view();

    void render_tab_bar();
    void render_welcome_tab();
    void render_analyzer_tab();
    void render_about_tab();
    void render_menu_bar();

    std::string open_file_dialog(const char* filter_pattern, const char* title);

    static void on_drop(GLFWwindow* window, int count, const char** paths);
};

} // namespace bmv
