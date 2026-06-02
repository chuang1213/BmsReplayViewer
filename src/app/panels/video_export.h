#pragma once
#include "core/timeline.h"
#include "replay/replay.h"
#include <cstdint>
#include <cstdio>
#include <vector>

struct GLFWwindow;

namespace bmv {

struct ExportConfig {
    int  width        = 1080;
    int  height       = 1920;
    int  fps          = 60;
    bool green_screen = false;
    char output_path[256] = "output.mp4";
};

class VideoExporter {
public:
    VideoExporter() = default;
    ~VideoExporter();

    bool   is_exporting() const { return exporting_; }
    float  progress()     const { return progress_; }
    const char* status_msg() const { return status_msg_; }

    void start_export(const ExportConfig& cfg,
                      const Timeline& tl,
                      const ReplayData* replay,
                      const int* bms_lane_to_display,
                      bool is_2p_layout,
                      bool show_replay,
                      double pixels_per_tick);

    void cancel_export();
    void process_frame();   // call each frame while exporting
    void render_ui();       // modal progress dialog

private:
    bool   exporting_   = false;
    float  progress_    = 0.0f;
    const char* status_msg_ = "";

    FILE*  pipe_        = nullptr;
    int    total_frames_ = 0;
    int    current_frame_ = 0;
    double frame_dt_    = 0.0;
    double cur_time_sec_ = 0.0;
    double cur_tick_     = 0.0;

    ExportConfig       cfg_;
    const Timeline*    timeline_   = nullptr;
    const ReplayData*  replay_     = nullptr;
    const int*         lane_map_   = nullptr;
    bool               is_2p_      = false;
    bool               show_replay_ = true;
    double             ppt_        = 0.1;

    std::vector<uint8_t> pixel_buf_;

    void render_frame_to_buf(uint8_t* buf);
};

} // namespace bmv
