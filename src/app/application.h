#pragma once
#include "core/timeline.h"
#include "replay/replay.h"
#include "app/panels/chart_view.h"
#include "app/panels/video_export.h"
#include <string>
#include <vector>

struct GLFWwindow;
struct ImFont;

namespace bmv {

class Application {
public:
    Application() = default;
    ~Application() = default;
    int run();
    void set_preload_chart(const std::string& utf8_path) { preload_chart_ = utf8_path; }

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

    std::string   bms_sha256_;
    std::string   bms_md5_;
    std::string   preload_chart_;  // GUI 启动后自动加载的谱面路径
    ImFont*       cjk_font_        = nullptr;  // 元数据栏日文显示用

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
    void render_analyzer_tab();
    void render_menu_bar();

    std::string open_file_dialog(const char* filter_pattern, const char* title);

    static void on_drop(GLFWwindow* window, int count, const char** paths);
};

} // namespace bmv
