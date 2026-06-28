#include "video_export.h"
#include "render/core_renderer.h"
#include "util/fs_util.h"
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

// ── Shared rendering via CoreRenderer ──

void VideoExporter::render_frame_to_buf(uint8_t* buf_data) {
    PixBuf pb;
    pb.data = buf_data;
    pb.w    = cfg_.width;
    pb.h    = cfg_.height;

    // Viewport adapter: tick → pixel Y (camera-relative)
    struct FrameViewport : public Viewport {
        tick_t cur_tick;
        double ppt;
        float  jy;
        int y_at(tick_t tick) const override {
            float dt = static_cast<float>(tick) - static_cast<float>(cur_tick);
            return static_cast<int>(jy - dt * static_cast<float>(ppt));
        }
    };

    // Buffer adapter: uint32_t → RGB bytes
    struct VideoBuf : public PixelBuf {
        PixBuf* pb = nullptr;
        void fill_rect(int x, int y, int w, int h, uint32_t color) override {
            RGB rgb;
            rgb.r = static_cast<uint8_t>(color & 0xFF);
            rgb.g = static_cast<uint8_t>((color >> 8) & 0xFF);
            rgb.b = static_cast<uint8_t>((color >> 16) & 0xFF);
            pb->fill_rect(x, y, w, h, rgb);
        }
    };

    FrameViewport vp;
    vp.width    = pb.w;
    vp.height   = pb.h;
    vp.cur_tick = static_cast<tick_t>(cur_tick_);
    vp.ppt      = ppt_;
    vp.jy       = static_cast<float>(pb.h) - 50.0f;

    VideoBuf vbuf;
    vbuf.pb = &pb;

    float kPadBottom = 50.0f;
    tick_t min_tick = static_cast<tick_t>(cur_tick_ - kPadBottom / ppt_);
    tick_t max_tick = static_cast<tick_t>(cur_tick_ + (pb.h - kPadBottom) / ppt_);
    if (min_tick < 0) min_tick = 0;

    // Color palette (convert from video_export's RGB constants to uint32_t)
    auto to_u32 = [](RGB c) -> uint32_t {
        return static_cast<uint32_t>(c.r) | (static_cast<uint32_t>(c.g) << 8)
               | (static_cast<uint32_t>(c.b) << 16) | 0xFF000000u;
    };

    GridColors gc;
    gc.bg_full   = to_u32(cfg_.green_screen ? C_GREEN : C_BG_FULL);
    gc.bg_a      = to_u32(C_BG_A);
    gc.bg_b      = to_u32(C_BG_B);
    gc.measure   = to_u32(C_MEASURE);
    gc.fourth    = to_u32(C_GRID_4);
    gc.eighth    = to_u32(C_GRID_8);
    gc.sixteenth = to_u32(C_GRID_16);
    gc.lane      = to_u32(C_LANE);
    gc.judgment  = to_u32(C_JUDGMENT);

    NoteColors nc;
    nc.scratch = to_u32(C_SCRATCH);
    nc.white   = to_u32(C_WHITE);
    nc.blue    = to_u32(C_BLUE);
    nc.ln_tail = to_u32(C_LN_TAIL);

    float cx0 = 40.0f;
    float lw  = 44.0f;
    int   nl  = 8;
    int   cw  = nl * static_cast<int>(lw);

    CoreRenderer::draw_background(vbuf, vp,
        min_tick, max_tick, cx0, cw, nl, lw, gc);

    // Build combined lane map including 2P shift if needed
    int combined_map[8];
    for (int i = 0; i < 8; ++i) {
        int dl = lane_map_ ? lane_map_[i] : i;
        if (is_2p_) dl = (dl == 0) ? 7 : (dl - 1);
        combined_map[i] = dl;
    }

    CoreRenderer::draw_notes(vbuf, vp,
        timeline_->notes, combined_map,
        min_tick, max_tick, cx0, lw, 28.0f, 4.0f, ppt_,
        nc, nl);

    if (show_replay_ && replay_) {
        CoreRenderer::draw_replay_hits(vbuf, vp,
            replay_->hits,
            min_tick, max_tick, cx0, lw, 20.0f, ppt_, nl,
            to_u32(C_REPLAY));
    }
}

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

#ifdef _WIN32
    // Windows: _popen 走 ANSI 代码页，非 ASCII 输出路径会失败。
    // 改用 _wpopen + 宽字符命令串，正确处理日文/中文输出路径。
    std::wstring wcmd = bmv::utf8_to_wstring(cmd);
    pipe_ = _wpopen(wcmd.c_str(), L"wb");
#else
    pipe_ = _popen(cmd, "wb");
#endif
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
