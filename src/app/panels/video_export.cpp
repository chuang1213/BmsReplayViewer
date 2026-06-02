#include "video_export.h"
#include "imgui.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifndef _WIN32
#include <unistd.h>
#define _popen  popen
#define _pclose pclose
#endif

namespace bmv {

// ── RGB color helpers ──
struct RGB { uint8_t r, g, b; };

static constexpr RGB C_SCRATCH  {255,  64,  64};
static constexpr RGB C_WHITE    {240, 240, 240};
static constexpr RGB C_BLUE     { 80, 120, 255};
static constexpr RGB C_REPLAY   {180, 180, 255};
static constexpr RGB C_BG_A     { 34,  34,  56};
static constexpr RGB C_BG_B     { 30,  30,  50};
static constexpr RGB C_BG_FULL  { 26,  26,  46};
static constexpr RGB C_GREEN    {  0, 255,   0};
static constexpr RGB C_MEASURE  { 85,  85, 119};
static constexpr RGB C_GRID_4   { 58,  58,  94};
static constexpr RGB C_GRID_8   { 46,  46,  80};
static constexpr RGB C_GRID_16  { 39,  39,  68};
static constexpr RGB C_LANE     { 58,  58,  94};
static constexpr RGB C_JUDGMENT {255,  80,  80};
static constexpr RGB C_LN_TAIL  {170, 170, 255};

// ── Software pixel buffer ──
struct PixBuf {
    uint8_t* data;
    int w, h;

    void set_pixel(int x, int y, RGB c) {
        if (x < 0 || x >= w || y < 0 || y >= h) return;
        size_t idx = (static_cast<size_t>(y) * w + x) * 3;
        data[idx]   = c.r;
        data[idx+1] = c.g;
        data[idx+2] = c.b;
    }

    void fill_rect(int x, int y, int rw, int rh, RGB c) {
        int x0 = std::max(x, 0);
        int y0 = std::max(y, 0);
        int x1 = std::min(x + rw, w);
        int y1 = std::min(y + rh, h);
        for (int py = y0; py < y1; ++py) {
            for (int px = x0; px < x1; ++px) {
                set_pixel(px, py, c);
            }
        }
    }

    void hline(int y, int x0, int x1, RGB c) {
        fill_rect(x0, y, x1 - x0, 1, c);
    }

    void vline(int x, int y0, int y1, RGB c) {
        fill_rect(x, y0, 1, y1 - y0, c);
    }

    void hollow_rect(int x, int y, int rw, int rh, RGB c, int thick) {
        fill_rect(x, y, rw, thick, c);                     // top
        fill_rect(x, y + rh - thick, rw, thick, c);        // bottom
        fill_rect(x, y, thick, rh, c);                     // left
        fill_rect(x + rw - thick, y, thick, rh, c);        // right
    }

    void fill(RGB c) {
        fill_rect(0, 0, w, h, c);
    }
};

static int ex_lane_to_column(int render_lane, bool is_2p) {
    if (!is_2p) return render_lane;
    return (render_lane == 0) ? 7 : (render_lane - 1);
}

// ── Software chart renderer ──

static void render_export_frame(
    PixBuf& buf,
    const Timeline& tl, const ReplayData* replay,
    tick_t cur_tick, double ppt, bool is_2p, bool show_replay,
    bool green_screen,
    const int* lane_map)
{
    constexpr int    kNumLanes      = 8;
    constexpr float  kPadBottom     = 50.0f;
    constexpr float  kLaneW         = 44.0f;
    constexpr float  kNoteW         = 28.0f;
    constexpr float  kNoteMinH      = 4.0f;
    constexpr float  kBoxW          = 20.0f;
    constexpr tick_t kBeatT         = 1920;
    constexpr tick_t kMeasureT      = 7680;

    float chart_x0 = 40.0f;
    float jy = static_cast<float>(buf.h) - kPadBottom; // judgment line

    auto screen_y = [&](tick_t t) -> float {
        float dt = static_cast<float>(t) - static_cast<float>(cur_tick);
        return jy - dt * static_cast<float>(ppt);
    };

    // ── Background ──
    buf.fill(green_screen ? C_GREEN : C_BG_FULL);

    // Viewport tick range for culling
    tick_t min_tick = static_cast<tick_t>(cur_tick - kPadBottom / ppt);
    tick_t max_tick = static_cast<tick_t>(cur_tick + (buf.h - kPadBottom) / ppt);
    if (min_tick < 0) min_tick = 0;

    // ── Measure backgrounds + grid ──
    {
        tick_t m_begin = min_tick / kMeasureT;
        tick_t m_end   = (max_tick / kMeasureT) + 1;
        float x0 = chart_x0;
        float x1 = x0 + kNumLanes * kLaneW;

        for (tick_t m = m_begin; m <= m_end; ++m) {
            RGB bg = (m % 2 == 0) ? C_BG_A : C_BG_B;
            float y0 = screen_y(m * kMeasureT + kMeasureT);
            float y1 = screen_y(m * kMeasureT);
            if (!green_screen)
                buf.fill_rect(static_cast<int>(x0), static_cast<int>(y1),
                              static_cast<int>(x1 - x0),
                              static_cast<int>(y0 - y1), bg);
        }

        // Grid lines
        tick_t t_beg = (min_tick / kMeasureT) * kMeasureT;
        tick_t t_end = (max_tick / kBeatT) * kBeatT + kBeatT;
        for (tick_t t = t_beg; t <= t_end; t += kBeatT) {
            int y = static_cast<int>(screen_y(t));
            bool is_meas = (t % kMeasureT == 0);
            RGB col = C_GRID_16;
            int thk = 1;
            if (is_meas) { col = C_MEASURE; thk = 2; }
            else if ((t % (kBeatT * 4)) == 0) { col = C_GRID_4; thk = 1; }
            else if ((t % (kBeatT * 2)) == 0) { col = C_GRID_8; thk = 1; }
            buf.fill_rect(static_cast<int>(x0), y,
                          static_cast<int>(x1 - x0), thk, col);
        }

        // Lane separators (vertical)
        for (int lane = 0; lane <= kNumLanes; ++lane) {
            int lx = static_cast<int>(chart_x0 + lane * kLaneW);
            int thk = (lane == 0 || lane == kNumLanes) ? 2 : 1;
            buf.vline(lx, 0, buf.h, C_LANE);
            // Note: vline thickness is always 1, so for 2px lines do fill_rect
            if (thk == 2) buf.fill_rect(lx, 0, 2, buf.h, C_LANE);
        }

        // Judgment line
        buf.fill_rect(static_cast<int>(x0), static_cast<int>(jy),
                      static_cast<int>(x1 - x0), 2, C_JUDGMENT);
    }

    // ── Notes ──
    float ppt_f = static_cast<float>(ppt);
    for (size_t i = 0; i < tl.notes.size(); ++i) {
        const auto& n = tl.notes[i];
        if (n.end_tick < min_tick && n.tick < min_tick) continue;
        if (n.tick > max_tick) continue;

        int render_lane = lane_map[n.lane];
        if (render_lane >= kNumLanes) continue;
        int col = ex_lane_to_column(render_lane, is_2p);

        RGB color;
        switch (n.lane) {
            case 0: color = C_SCRATCH; break;
            case 1: case 3: case 5: case 7: color = C_WHITE; break;
            default: color = C_BLUE; break;
        }

        int cx = static_cast<int>(chart_x0 + col * kLaneW + (kLaneW - kNoteW) * 0.5f);
        int cy = static_cast<int>(screen_y(n.tick));
        int nh = std::max(static_cast<int>(kNoteMinH),
                          static_cast<int>(ppt_f * 6.0f));
        int ny = cy - nh / 2;
        buf.fill_rect(cx, ny, static_cast<int>(kNoteW), nh, color);

        // LN body
        if (n.end_tick > n.tick) {
            int body_x  = cx + static_cast<int>(kNoteW * 0.3f);
            int body_w  = static_cast<int>(kNoteW * 0.4f);
            int tail_y  = static_cast<int>(screen_y(n.end_tick));
            int body_y0 = cy - nh / 2 + nh / 2;
            int body_y1 = tail_y;

            int vis_top = static_cast<int>(screen_y(max_tick));
            int vis_bot = static_cast<int>(screen_y(min_tick));
            if (body_y1 > vis_bot) body_y1 = vis_bot;
            if (body_y0 < vis_top) body_y0 = vis_top;

            if (body_y1 < body_y0) {
                buf.fill_rect(body_x, body_y1, body_w, body_y0 - body_y1, color);
            }

            int tail_mark_y = tail_y - 1;
            if (tail_mark_y < vis_top) tail_mark_y = vis_top;
            if (tail_mark_y + 3 > vis_bot) tail_mark_y = vis_bot - 3;
            buf.fill_rect(cx, tail_mark_y, static_cast<int>(kNoteW), 3, C_LN_TAIL);
        }
    }

    // ── Replay hits ──
    if (show_replay && replay) {
        for (size_t i = 0; i < replay->hits.size(); ++i) {
            const auto& hit = replay->hits[i];
            if (hit.tick_end < min_tick && hit.tick_start < min_tick) continue;
            if (hit.tick_start > max_tick) continue;
            if (hit.lane >= static_cast<uint8_t>(kNumLanes)) continue;

            int col = ex_lane_to_column(hit.lane, is_2p);

            tick_t ts = hit.tick_start;
            tick_t te = (hit.tick_end > hit.tick_start) ? hit.tick_end
                                                         : (hit.tick_start + 1);

            int sy0 = static_cast<int>(screen_y(ts));
            int sy1 = static_cast<int>(screen_y(te));
            int vtop = static_cast<int>(screen_y(max_tick));
            int vbot = static_cast<int>(screen_y(min_tick));
            if (sy1 > vbot) sy1 = vbot;
            if (sy0 < vtop) sy0 = vtop;

            int bh = sy0 - sy1;
            int min_h = std::max(2, static_cast<int>(ppt_f * 4.0f));
            if (bh < min_h) bh = min_h;

            int bx = static_cast<int>(chart_x0 + col * kLaneW
                      + (kLaneW - kBoxW) * 0.5f);
            int by = sy1;

            buf.hollow_rect(bx, by, static_cast<int>(kBoxW), bh, C_REPLAY, 1);
        }
    }
}

// ── VideoExporter ──

VideoExporter::~VideoExporter() {
    cancel_export();
}

void VideoExporter::start_export(const ExportConfig& cfg,
                                  const Timeline& tl,
                                  const ReplayData* replay,
                                  const int* bms_lane_to_display,
                                  bool is_2p_layout,
                                  bool show_replay,
                                  double pixels_per_tick) {
    cfg_          = cfg;
    timeline_     = &tl;
    replay_       = replay;
    lane_map_     = bms_lane_to_display;
    is_2p_        = is_2p_layout;
    show_replay_  = show_replay;
    ppt_          = pixels_per_tick;

    double total_sec = tl.time_map.tick_to_second(tl.tick_end());
    frame_dt_    = 1.0 / cfg.fps;
    total_frames_ = static_cast<int>(total_sec * cfg.fps) + 1;
    current_frame_ = 0;
    cur_time_sec_  = 0.0;
    progress_      = 0.0f;

    // Build ffmpeg command
    char cmd[1024];
    std::snprintf(cmd, sizeof(cmd),
        "ffmpeg -y -f rawvideo -vcodec rawvideo -pix_fmt rgb24 "
        "-s %dx%d -r %d -i - "
        "-c:v libx264 -preset fast -crf 18 \"%s\"",
        cfg.width, cfg.height, cfg.fps, cfg.output_path);

    pipe_ = _popen(cmd, "wb");
    if (!pipe_) {
        status_msg_ = "Failed to launch ffmpeg. Is it installed?";
        exporting_  = false;
        return;
    }

    pixel_buf_.resize(static_cast<size_t>(cfg.width) * cfg.height * 3);
    exporting_   = true;
    status_msg_  = "Exporting...";
}

void VideoExporter::cancel_export() {
    if (pipe_) {
        _pclose(pipe_);
        pipe_ = nullptr;
    }
    exporting_ = false;
    status_msg_ = "Cancelled";
}

void VideoExporter::process_frame() {
    if (!exporting_ || !timeline_) return;

    cur_tick_ = timeline_->time_map.second_to_tick(cur_time_sec_);

    render_frame_to_buf(pixel_buf_.data());

    size_t frame_bytes = static_cast<size_t>(cfg_.width) * cfg_.height * 3;
    fwrite(pixel_buf_.data(), 1, frame_bytes, pipe_);

    current_frame_++;
    cur_time_sec_ = current_frame_ * frame_dt_;

    if (current_frame_ >= total_frames_) {
        _pclose(pipe_);
        pipe_       = nullptr;
        exporting_  = false;
        progress_   = 1.0f;
        status_msg_ = "Export complete!";
    } else {
        progress_ = static_cast<float>(current_frame_) / total_frames_;
    }
}

void VideoExporter::render_frame_to_buf(uint8_t* buf) {
    PixBuf pb;
    pb.data = buf;
    pb.w    = cfg_.width;
    pb.h    = cfg_.height;

    render_export_frame(pb, *timeline_, replay_,
        static_cast<tick_t>(cur_tick_), ppt_,
        is_2p_, show_replay_, cfg_.green_screen, lane_map_);
}

void VideoExporter::render_ui() {
    if (!exporting_) return;

    ImGui::OpenPopup("Exporting Video##export_modal");

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(400, 150), ImGuiCond_Appearing);

    if (ImGui::BeginPopupModal("Exporting Video##export_modal", nullptr,
                                ImGuiWindowFlags_NoResize |
                                ImGuiWindowFlags_NoMove)) {
        ImGui::Text("Rendering frame %d / %d", current_frame_, total_frames_);
        ImGui::ProgressBar(progress_, ImVec2(-1, 0));
        ImGui::Text("%s", status_msg_);

        if (ImGui::Button("Cancel")) {
            cancel_export();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

} // namespace bmv
