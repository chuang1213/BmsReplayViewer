#pragma once
#include "core/timeline.h"
#include "replay/replay.h"
#include <string>
#include <cstdint>
#include <vector>

namespace bmv {

class PngRenderer {
public:
    struct Config {
        int pixels_per_measure = 300;

        int lane_width     = 36;
        int note_width     = 24;
        int note_min_height = 6;
        bool note_height_proportional = false;

        int margin_top    = 40;
        int margin_bottom = 20;
        int margin_left   = 48;
        int margin_right  = 80;       // expanded for labels

        // Colors (AABBGGRR convention in memory)
        uint32_t color_bg           = 0xFF1A1A2E;
        uint32_t color_measure_bg_a = 0xFF222238;
        uint32_t color_measure_bg_b = 0xFF1E1E32;
        uint32_t color_measure_sep  = 0xFF555577;
        uint32_t color_grid_4th     = 0xFF3A3A5E;
        uint32_t color_grid_8th     = 0xFF2E2E50;
        uint32_t color_grid_16th    = 0xFF272744;
        uint32_t color_lane_sep     = 0xFF3A3A5E;

        uint32_t note_color_scratch = 0xFF4040FF;
        uint32_t note_color_white   = 0xFFF0F0F0;
        uint32_t note_color_blue    = 0xFFFF7850;

        uint32_t color_bpm_line     = 0xFF50C850;   // R=80 G=200 B=80
        uint32_t color_stop_line    = 0xFF50B4DC;   // R=220 G=180 B=80
        uint32_t color_measure_num  = 0xFFFFFFFF;   // white
        uint32_t color_label_text   = 0xFFAAAAFF;

        // Replay overlay
        uint32_t replay_box_color   = 0xFFB4B4FF;   // light blue-purple hollow box
        int      replay_box_width   = 18;
        int      replay_box_min_h   = 4;
    };

    PngRenderer() = default;

    bool render(const Timeline& timeline,
                const ReplayData* replay,
                const Config& config,
                const std::string& output_path);

private:
    struct PixelBuffer {
        std::vector<uint32_t> pixels;
        int width  = 0;
        int height = 0;

        void init(int w, int h);
        void fill(uint32_t color);
        void fill_rect(int x, int y, int w, int h, uint32_t color);
        void fill_rect_clipped(int x, int y, int w, int h, uint32_t color,
                               int clip_x, int clip_y, int clip_w, int clip_h);
    };

    PixelBuffer buf_;
    Config      cfg_;
    int         num_lanes_       = 8;
    int         num_measures_    = 0;
    double      pixels_per_tick_ = 0.0;
    int         bms_lane_to_display_[8] = {0,1,2,3,4,5,6,7};

    int  measure_top(int m) const;
    int  measure_bottom(int m) const;
    int  chart_y_abs(int measure, tick_t tick_in_measure) const;
    tick_t tick_in_measure(tick_t abs_tick) const;
    int  measure_of(tick_t abs_tick) const;

    int chart_x(int lane) const;
    int chart_w() const;
    int chart_h() const;
    int chart_x0() const { return cfg_.margin_left; }
    int chart_y0() const;
    int label_x0() const { return chart_x0() + chart_w() + 6; }

    // 5×7 bitmap font for digits 0-9
    static const uint8_t FONT_DIGIT[10][7];
    void draw_char(int x, int y, char ch, uint32_t color);
    void draw_text(int x, int y, const std::string& text, uint32_t color);

    void draw_measure_backgrounds();
    void draw_grids();
    void draw_measure_separators();
    void draw_lane_separators();
    void draw_notes(const Timeline& tl);
    void draw_bpm_lines(const Timeline& tl);
    void draw_stop_lines(const Timeline& tl);
    void draw_measure_numbers(const Timeline& tl);
    void draw_replay_hits(const ReplayData& replay);
};

} // namespace bmv
