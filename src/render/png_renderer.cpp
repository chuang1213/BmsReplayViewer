#include "png_renderer.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

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

void PngRenderer::PixelBuffer::fill_rect_clipped(
    int x, int y, int w, int h, uint32_t color,
    int cx, int cy, int cw, int ch)
{
    if (w <= 0 || h <= 0) return;
    int x0 = std::max(x, cx);
    int y0 = std::max(y, cy);
    int x1 = std::min(x + w, cx + cw);
    int y1 = std::min(y + h, cy + ch);
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

// --- Drawing ---

void PngRenderer::draw_measure_backgrounds() {
    for (int m = 0; m < num_measures_; ++m) {
        int y = measure_top(m);
        uint32_t bg = (m % 2 == 0) ? cfg_.color_measure_bg_a : cfg_.color_measure_bg_b;
        buf_.fill_rect(chart_x0(), y, chart_w(), cfg_.pixels_per_measure, bg);
    }
}

void PngRenderer::draw_grids() {
    for (int m = 0; m < num_measures_; ++m) {
        int meas_bottom = measure_bottom(m);

        tick_t step_16 = TICKS_PER_MEASURE / 16;
        for (int i = 0; i <= 16; ++i) {
            int y = meas_bottom - static_cast<int>(i * step_16 * pixels_per_tick_);
            buf_.fill_rect(chart_x0(), y, chart_w(), 1, cfg_.color_grid_16th);
        }

        tick_t step_8 = TICKS_PER_MEASURE / 8;
        for (int i = 0; i <= 8; ++i) {
            int y = meas_bottom - static_cast<int>(i * step_8 * pixels_per_tick_);
            buf_.fill_rect(chart_x0(), y, chart_w(), 1, cfg_.color_grid_8th);
        }

        tick_t step_4 = TICKS_PER_MEASURE / 4;
        for (int i = 0; i <= 4; ++i) {
            int y = meas_bottom - static_cast<int>(i * step_4 * pixels_per_tick_);
            buf_.fill_rect(chart_x0(), y, chart_w(), 1, cfg_.color_grid_4th);
        }
    }
}

void PngRenderer::draw_measure_separators() {
    for (int m = 0; m <= num_measures_; ++m) {
        int y = measure_bottom(m);
        int thickness = 2;
        buf_.fill_rect(chart_x0(), y, chart_w(), thickness, cfg_.color_measure_sep);
    }
}

void PngRenderer::draw_lane_separators() {
    for (int lane = 0; lane <= num_lanes_; ++lane) {
        int x = chart_x(lane);
        int thickness = (lane == 0 || lane == num_lanes_) ? 2 : 1;
        buf_.fill_rect(x, chart_y0(), thickness, chart_h(), cfg_.color_lane_sep);
    }
}

void PngRenderer::draw_notes(const Timeline& tl) {
    for (auto& n : tl.notes) {
        int render_lane = bms_lane_to_display_[n.lane];
        if (render_lane >= static_cast<int>(num_lanes_)) continue;

        int meas   = measure_of(n.tick);
        tick_t t_in = tick_in_measure(n.tick);

        if (meas < 0 || meas >= num_measures_) continue;

        int x = chart_x(render_lane) + (cfg_.lane_width - cfg_.note_width) / 2;

        int note_h = cfg_.note_min_height;
        int y_center = chart_y_abs(meas, t_in);

        if (cfg_.note_height_proportional && n.end_tick > n.tick) {
            int m_end = measure_of(n.end_tick);
            tick_t t_end = tick_in_measure(n.end_tick);
            if (m_end == meas) {
                int y_end = chart_y_abs(meas, t_end);
                note_h = std::max(y_center - y_end, cfg_.note_min_height);
            } else if (m_end > meas) {
                note_h = std::max(y_center - measure_top(meas), cfg_.note_min_height);
            }
        }

        uint32_t color;
        switch (n.lane) {
            case 0: color = cfg_.note_color_scratch; break;
            case 1: case 3: case 5: case 7:
                color = cfg_.note_color_white; break;
            default:
                color = cfg_.note_color_blue; break;
        }

        int y = y_center - note_h / 2;
        int meas_top_y = measure_top(meas);
        buf_.fill_rect_clipped(x, y, cfg_.note_width, note_h, color,
                                chart_x0(), meas_top_y, chart_w(), cfg_.pixels_per_measure);

        // LN body: draw a vertical bar from head tick to end_tick
        if (n.end_tick > n.tick) {
            int m_tail = measure_of(n.end_tick);
            tick_t t_tail = tick_in_measure(n.end_tick);
            int body_x = x + cfg_.note_width / 2 - 2;  // narrow bar in center of lane

            for (int m = meas; m <= m_tail && m < num_measures_; ++m) {
                int seg_y0, seg_y1;
                if (m == meas) {
                    seg_y0 = y_center;              // head center
                    // If LN ends in same measure, stop at tail; otherwise fill to measure top
                    seg_y1 = (meas == m_tail) ? chart_y_abs(m, t_tail) : measure_top(m);
                } else if (m == m_tail) {
                    seg_y0 = measure_bottom(m);      // bottom of tail measure
                    seg_y1 = chart_y_abs(m, t_tail); // tail position
                } else {
                    seg_y0 = measure_bottom(m);
                    seg_y1 = measure_top(m);
                }
                if (seg_y1 < seg_y0) {
                    int meas_top_y2 = measure_top(m);
                    buf_.fill_rect_clipped(body_x, seg_y1, 4, seg_y0 - seg_y1, color,
                                            chart_x0(), meas_top_y2, chart_w(), cfg_.pixels_per_measure);
                }
            }

            // LN tail marker (small rectangle at end)
            int tail_y = chart_y_abs(m_tail, t_tail) - 2;
            int tail_m_top = measure_top(m_tail);
            buf_.fill_rect_clipped(x, tail_y, cfg_.note_width, 4, cfg_.color_label_text,
                                    chart_x0(), tail_m_top, chart_w(), cfg_.pixels_per_measure);
        }
    }
}

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

void PngRenderer::draw_replay_hits(const ReplayData& replay) {
    for (auto& hit : replay.hits) {
        if (hit.lane >= static_cast<uint8_t>(num_lanes_)) continue;

        tick_t t_start = hit.tick_start;
        tick_t t_end   = (hit.tick_end > hit.tick_start) ? hit.tick_end
                                                          : (hit.tick_start + 1);

        int m_start = measure_of(t_start);
        int m_end   = measure_of(t_end);
        tick_t t_s  = tick_in_measure(t_start);
        tick_t t_e  = tick_in_measure(t_end);

        int y_start = chart_y_abs(m_start, t_s);  // bottom of box (larger Y)
        int y_end   = chart_y_abs(m_end, t_e);    // top of box (smaller Y)

        int box_x = chart_x(hit.lane) + (cfg_.lane_width - cfg_.replay_box_width) / 2;
        int box_w = cfg_.replay_box_width;
        uint32_t color = cfg_.replay_box_color;

        // Draw hollow box segments per measure
        for (int m = m_start; m <= m_end && m < num_measures_; ++m) {
            int seg_y0, seg_y1;
            if (m == m_start) {
                seg_y0 = y_start;
                seg_y1 = (m_start == m_end) ? y_end : measure_top(m);
            } else if (m == m_end) {
                seg_y0 = measure_bottom(m);
                seg_y1 = y_end;
            } else {
                seg_y0 = measure_bottom(m);
                seg_y1 = measure_top(m);
            }

            if (seg_y1 >= seg_y0) continue;
            int box_h = seg_y0 - seg_y1;
            if (box_h < cfg_.replay_box_min_h) box_h = cfg_.replay_box_min_h;

            int yt = seg_y1;           // top of box segment
            int yb = seg_y1 + box_h;   // bottom of box segment

            // hollow rectangle: 4 lines
            buf_.fill_rect(box_x, yt, box_w, 1, color);             // top
            buf_.fill_rect(box_x, yb - 1, box_w, 1, color);         // bottom
            buf_.fill_rect(box_x, yt, 1, box_h, color);             // left
            buf_.fill_rect(box_x + box_w - 1, yt, 1, box_h, color); // right
        }
    }

    std::fprintf(stdout, "[replay] %zu hits rendered\n", replay.hits.size());
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
        for (int i = 0; i < 8; ++i) {
            int original_val = replay->shuffle_pattern[i];
            int bms_lane = (original_val == 7) ? 0 : (original_val + 1);
            int display_lane = (i == 7) ? 0 : (i + 1);
            bms_lane_to_display_[bms_lane] = display_lane;
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

    draw_measure_backgrounds();
    draw_grids();
    draw_lane_separators();
    draw_measure_separators();
    draw_notes(timeline);
    draw_bpm_lines(timeline);       // after notes
    draw_stop_lines(timeline);      // after notes
    if (replay) {
        draw_replay_hits(*replay);  // replay overlay
    }
    draw_measure_numbers(timeline); // on top

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
