#include "png_renderer.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

#include "core_renderer.h"
#include "replay/lr2_random.h"
#include <algorithm>
#include <cstdio>
#include <cstring>

namespace bmv {

// --- 5×7 bitmap font for digits 0-9 ---
// Each byte is one row, MSB=leftmost pixel
const uint8_t PngRenderer::FONT_DIGIT[10][7] = {
    {0x1C,0x22,0x22,0x22,0x22,0x22,0x1C},  // 0
    {0x08,0x18,0x08,0x08,0x08,0x08,0x1C},  // 1
    {0x1C,0x22,0x02,0x04,0x08,0x10,0x3E},  // 2
    {0x1C,0x22,0x02,0x0C,0x02,0x22,0x1C},  // 3
    {0x04,0x0C,0x14,0x24,0x3E,0x04,0x04},  // 4
    {0x3E,0x20,0x3C,0x02,0x02,0x22,0x1C},  // 5
    {0x0C,0x10,0x20,0x3C,0x22,0x22,0x1C},  // 6
    {0x3E,0x02,0x04,0x08,0x10,0x10,0x10},  // 7
    {0x1C,0x22,0x22,0x1C,0x22,0x22,0x1C},  // 8
    {0x1C,0x22,0x22,0x1E,0x02,0x04,0x18},  // 9
};

void PngRenderer::draw_char(int x, int y, char ch, uint32_t color) {
    if (ch < '0' || ch > '9') return;
    int idx = ch - '0';
    for (int row = 0; row < 7; ++row) {
        uint8_t bits = FONT_DIGIT[idx][row];
        for (int col = 0; col < 5; ++col) {
            if (bits & (1 << (4 - col))) {
                buf_.fill_rect(x + col, y + row, 1, 1, color);
            }
        }
    }
}

void PngRenderer::draw_text(int x, int y, const std::string& text, uint32_t color) {
    int cx = x;
    for (char ch : text) {
        if (ch == ' ') { cx += 4; continue; }
        if (ch == '.') {
            buf_.fill_rect(cx + 1, y + 5, 2, 2, color);
            cx += 4; continue;
        }
        draw_char(cx, y, ch, color);
        cx += 6;  // 5px glyph + 1px gap
    }
}

// --- PixelBuffer ---

void PngRenderer::PixelBuffer::init(int w, int h) {
    width  = w;
    height = h;
    pixels.assign(static_cast<size_t>(w) * h, 0xFF000000);
}

void PngRenderer::PixelBuffer::fill(uint32_t color) {
    for (auto& p : pixels) p = color;
}

void PngRenderer::PixelBuffer::fill_rect(int x, int y, int w, int h, uint32_t color) {
    if (w <= 0 || h <= 0) return;
    int x0 = std::max(x, 0);
    int y0 = std::max(y, 0);
    int x1 = std::min(x + w, width);
    int y1 = std::min(y + h, height);
    for (int py = y0; py < y1; ++py) {
        auto* row = &pixels[py * width];
        for (int px = x0; px < x1; ++px) {
            row[px] = color;
        }
    }
}

// --- Coordinates ---

int PngRenderer::measure_top(int m) const {
    return cfg_.margin_top + (num_measures_ - 1 - m) * cfg_.pixels_per_measure;
}

int PngRenderer::measure_bottom(int m) const {
    return measure_top(m) + cfg_.pixels_per_measure;
}

int PngRenderer::chart_y_abs(int measure, tick_t t_in) const {
    return measure_bottom(measure) - static_cast<int>(t_in * pixels_per_tick_);
}

tick_t PngRenderer::tick_in_measure(tick_t abs_tick) const {
    if (abs_tick < 0) return 0;
    return abs_tick % TICKS_PER_MEASURE;
}

int PngRenderer::measure_of(tick_t abs_tick) const {
    if (abs_tick < 0) return 0;
    return static_cast<int>(abs_tick / TICKS_PER_MEASURE);
}

int PngRenderer::chart_x(int lane) const {
    return cfg_.margin_left + lane * cfg_.lane_width;
}

int PngRenderer::chart_w() const {
    return num_lanes_ * cfg_.lane_width;
}

int PngRenderer::chart_h() const {
    return num_measures_ * cfg_.pixels_per_measure;
}

int PngRenderer::chart_y0() const {
    return measure_top(num_measures_ - 1);
}

// --- Drawing (shared via CoreRenderer) ---

void PngRenderer::draw_bpm_lines(const Timeline& tl) {
    for (auto& b : tl.bpm_changes) {
        int meas   = measure_of(b.tick);
        tick_t t_in = tick_in_measure(b.tick);
        if (meas < 0 || meas >= num_measures_) continue;

        int y = chart_y_abs(meas, t_in);
        // Green horizontal line across lanes
        buf_.fill_rect(chart_x0(), y, chart_w(), 2, cfg_.color_bpm_line);
        // Label on the right
        char lbl[32];
        std::snprintf(lbl, sizeof(lbl), "BPM %.1f", b.bpm);
        draw_text(label_x0(), y - 3, lbl, cfg_.color_bpm_line);
    }
}

void PngRenderer::draw_stop_lines(const Timeline& tl) {
    for (auto& s : tl.stops) {
        int meas   = measure_of(s.tick);
        tick_t t_in = tick_in_measure(s.tick);
        if (meas < 0 || meas >= num_measures_) continue;

        int y = chart_y_abs(meas, t_in);
        // Yellow horizontal line across lanes
        buf_.fill_rect(chart_x0(), y, chart_w(), 2, cfg_.color_stop_line);
        // Label
        char lbl[32];
        std::snprintf(lbl, sizeof(lbl), "STOP %.3f", s.stop_beats);
        draw_text(label_x0(), y - 3, lbl, cfg_.color_stop_line);
    }
}

void PngRenderer::draw_measure_numbers(const Timeline& tl) {
    for (auto& m : tl.measures) {
        if (m.measure_num >= num_measures_) break;
        // Position at bottom of measure (which is the start in reversed layout)
        int y = measure_bottom(m.measure_num) - 10;
        char lbl[8];
        std::snprintf(lbl, sizeof(lbl), "%d", m.measure_num);
        draw_text(label_x0(), y, lbl, cfg_.color_measure_num);
    }
}

// --- Main render ---

bool PngRenderer::render(const Timeline& timeline,
                          const ReplayData* replay,
                          const Config& config,
                          const std::string& output_path)
{
    cfg_ = config;
    num_lanes_ = 8;

    num_measures_ = static_cast<int>(timeline.measures.size());
    if (num_measures_ < 1) num_measures_ = 1;

    // Build lane shuffle table from replay data (chart notes get shuffled; replay hits stay physical)
    for (int i = 0; i < 8; ++i) bms_lane_to_display_[i] = i;  // default 1:1
    if (replay && replay->has_shuffle) {
        // shuffle_pattern 已经是统一格式：shuffle_pattern[display_lane] = bms_lane
        // 其中 0 = scratch, 1-7 = keys
        for (int i = 0; i < 8; ++i) {
            int bms_lane = replay->shuffle_pattern[i];
            bms_lane_to_display_[bms_lane] = i;
        }
    }
    if (replay && replay->format == ReplayFormat::LR2REP && replay->has_random_info[0]) {
        int display_to_bms[8];
        int bms_to_display[8];
        for (int i = 0; i < 8; ++i) { display_to_bms[i] = i; bms_to_display[i] = i; }
        switch (replay->random_mode[0]) {
            case LR2RandomMode::Mirror:
                bms_to_display[1] = 7; bms_to_display[2] = 6;
                bms_to_display[3] = 5; bms_to_display[4] = 4;
                bms_to_display[5] = 3; bms_to_display[6] = 2;
                bms_to_display[7] = 1;
                for (int i = 0; i < 8; ++i) bms_lane_to_display_[i] = bms_to_display[i];
                break;
            case LR2RandomMode::Random:
                build_random_lane_pattern(replay->random_seed, 7, display_to_bms, bms_to_display);
                for (int i = 0; i < 8; ++i) bms_lane_to_display_[i] = bms_to_display[i];
                break;
            default:
                break;
        }
    }

    pixels_per_tick_ = static_cast<double>(cfg_.pixels_per_measure) / TICKS_PER_MEASURE;

    int img_w = chart_x0() + chart_w() + cfg_.margin_right;
    int img_h = chart_y0() + chart_h() + cfg_.margin_bottom;

    buf_.init(img_w, img_h);
    buf_.fill(cfg_.color_bg);

    // Core renderer via local adapters
    {
        struct PngViewport : public Viewport {
            PngRenderer* pr;
            int y_at(tick_t tick) const override {
                return pr->chart_y_abs(pr->measure_of(tick), pr->tick_in_measure(tick));
            }
        };
        struct PngBuf : public PixelBuf {
            PixelBuffer* pb = nullptr;
            void fill_rect(int x, int y, int w, int h, uint32_t c) override {
                pb->fill_rect(x, y, w, h, c);
            }
        };

        PngViewport vp;
        vp.pr     = this;
        vp.width  = img_w;
        vp.height = img_h;

        PngBuf pbuf;
        pbuf.pb = &buf_;

        GridColors gc;
        gc.bg_full   = cfg_.color_bg;
        gc.bg_a      = cfg_.color_measure_bg_a;
        gc.bg_b      = cfg_.color_measure_bg_b;
        gc.measure   = cfg_.color_measure_sep;
        gc.fourth    = cfg_.color_grid_4th;
        gc.eighth    = cfg_.color_grid_8th;
        gc.sixteenth = cfg_.color_grid_16th;
        gc.lane      = cfg_.color_lane_sep;
        gc.judgment  = cfg_.color_label_text;

        NoteColors nc;
        nc.scratch = cfg_.note_color_scratch;
        nc.white   = cfg_.note_color_white;
        nc.blue    = cfg_.note_color_blue;
        nc.ln_tail = cfg_.color_label_text;

        tick_t min_tick = 0;
        tick_t max_tick = timeline.tick_end();

        CoreRenderer::draw_background(pbuf, vp,
            min_tick, max_tick,
            static_cast<float>(chart_x0()), chart_w(),
            num_lanes_, static_cast<float>(cfg_.lane_width), gc);

        CoreRenderer::draw_notes(pbuf, vp,
            timeline.notes, bms_lane_to_display_,
            min_tick, max_tick,
            static_cast<float>(chart_x0()),
            static_cast<float>(cfg_.lane_width),
            static_cast<float>(cfg_.note_width),
            static_cast<float>(cfg_.note_min_height),
            pixels_per_tick_, nc, num_lanes_);

        if (replay) {
            CoreRenderer::draw_replay_hits(pbuf, vp,
                replay->hits,
                min_tick, max_tick,
                static_cast<float>(chart_x0()),
                static_cast<float>(cfg_.lane_width),
                static_cast<float>(cfg_.replay_box_width),
                pixels_per_tick_, num_lanes_, cfg_.replay_box_color);
        }
    }

    draw_bpm_lines(timeline);
    draw_stop_lines(timeline);
    draw_measure_numbers(timeline);

    int stride = img_w * 4;
    int result = stbi_write_png(output_path.c_str(), img_w, img_h, 4,
                                 buf_.pixels.data(), stride);
    if (result == 0) {
        std::fprintf(stderr, "Error: failed to write PNG '%s'\n", output_path.c_str());
        return false;
    }

    std::fprintf(stdout, "[info] rendered %dx%d PNG, %d measures, %zu notes, "
                 "%zu BPM, %zu STOP\n",
                 img_w, img_h, num_measures_, timeline.notes.size(),
                 timeline.bpm_changes.size(), timeline.stops.size());
    return true;
}

} // namespace bmv
