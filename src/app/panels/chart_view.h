#pragma once
#include "core/timeline.h"
#include "replay/replay.h"
#include "analysis/judgement_engine.h"
#include "imgui.h"

namespace bmv {

enum class ReplayDisplayMode {
    LineOnly,
    FullHold,
    Marker
};

class ChartView {
public:
    ChartView() = default;

    struct Config {
        bool show_replay  = true;
        bool is_2p_layout = false;
    };

    Config config;

    void set_data(const Timeline* timeline, const ReplayData* replay);

    void render_analyzer();
    void render_controls_window();

    static int lane_to_column(int render_lane, bool is_2p);

    double  current_tick()      const { return current_tick_; }
    double  pixels_per_tick()   const { return pixels_per_tick_; }
    double  current_time_sec()  const { return current_time_sec_; }
    bool    is_playing()        const { return is_playing_; }
    const int* bms_lane_to_display() const { return bms_lane_to_display_; }

    void    set_pixels_per_tick(double v) { pixels_per_tick_ = v; }

    float  note_thickness()        const { return note_thickness_; }
    int    scroll_distance()       const { return scroll_distance_; }
    int    replay_display_mode_int() const { return static_cast<int>(replay_display_mode_); }
    bool   auto_follow_playback()  const { return auto_follow_playback_; }
    void   set_note_thickness(float v)       { note_thickness_ = v; }
    void   set_scroll_distance(int v)        { scroll_distance_ = v; }
    void   set_replay_display_mode(int v)    { replay_display_mode_ = static_cast<ReplayDisplayMode>(v); }
    void   set_auto_follow_playback(bool v)  { auto_follow_playback_ = v; }

private:
    const Timeline* timeline_ = nullptr;
    const ReplayData* replay_ = nullptr;

    JudgementEngine judge_engine_;

    double current_tick_      = 0.0;
    double pixels_per_tick_   = 0.1;
    bool   is_playing_        = false;
    double current_time_sec_  = 0.0;
    int    bms_lane_to_display_[8] = {0, 1, 2, 3, 4, 5, 6, 7};

    int    player_idx_                = 0;   // 0=P1, 1=P2

    float  note_thickness_           = 6.0f;
    int    scroll_distance_          = 7680;
    bool   auto_follow_playback_     = true;
    bool   show_releases_            = false;
    ReplayDisplayMode replay_display_mode_ = ReplayDisplayMode::LineOnly;

    static constexpr int    kNumLanes       = 8;
    static constexpr float  kPaddingBottom  = 50.0f;
    static constexpr float  kLaneWidth      = 44.0f;
    static constexpr float  kNoteWidth      = 28.0f;
    static constexpr float  kNoteMinHeight  = 4.0f;
    static constexpr float  kReplayBoxWidth = 20.0f;
    static constexpr tick_t kBeatTick       = 1920;
    static constexpr tick_t kMeasureTick    = 7680;

    float   chart_x0()   const { return 8.0f; }
    float   chart_width() const { return kNumLanes * kLaneWidth; }
    float   screen_y(tick_t tick) const;

    void draw_background(ImDrawList* dl, const ImVec2& win_pos, const ImVec2& win_size);
    void draw_grid_and_measures(ImDrawList* dl, const ImVec2& win_pos, const ImVec2& win_size);
    void draw_notes(ImDrawList* dl, const ImVec2& win_pos, const ImVec2& win_size);
    void draw_replay_hits(ImDrawList* dl, const ImVec2& win_pos, const ImVec2& win_size);
    void draw_miss_notes(ImDrawList* dl, const ImVec2& win_pos, const ImVec2& win_size);
    void handle_input();
    void render_controls_inline();
};

} // namespace bmv
