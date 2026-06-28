#include "chart_view.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace bmv {

static constexpr ImU32 COL_SCRATCH    = IM_COL32(255,  64,  64, 255);
static constexpr ImU32 COL_WHITE      = IM_COL32(240, 240, 240, 255);
static constexpr ImU32 COL_BLUE       = IM_COL32( 80, 120, 255, 255);
static constexpr ImU32 COL_REPLAY     = IM_COL32(180, 180, 255, 180);
static constexpr ImU32 COL_BG_A       = IM_COL32( 34,  34,  56, 255);
static constexpr ImU32 COL_BG_B       = IM_COL32( 30,  30,  50, 255);
static constexpr ImU32 COL_BG_FULL    = IM_COL32( 26,  26,  46, 255);
static constexpr ImU32 COL_MEASURE    = IM_COL32( 85,  85, 119, 255);
static constexpr ImU32 COL_GRID_4     = IM_COL32( 58,  58,  94, 255);
static constexpr ImU32 COL_GRID_8     = IM_COL32( 46,  46,  80, 255);
static constexpr ImU32 COL_GRID_16    = IM_COL32( 39,  39,  68, 255);
static constexpr ImU32 COL_LANE       = IM_COL32( 58,  58,  94, 255);
static constexpr ImU32 COL_MEASURE_NUM = IM_COL32(255, 255, 255, 255);
static constexpr ImU32 COL_LN_TAIL    = IM_COL32(170, 170, 255, 255);
static constexpr ImU32 COL_JUDGMENT   = IM_COL32(255,  80,  80, 150);

int ChartView::lane_to_column(int render_lane, bool is_2p) {
    if (!is_2p) return render_lane;
    return (render_lane == 0) ? 7 : (render_lane - 1);
}

static ImU32 judge_box_color(Judge jg, bool fast) {
    switch (jg) {
        case Judge::PGREAT:
            return IM_COL32( 80, 255,  80, 200);
        case Judge::GREAT:
            return fast ? IM_COL32(120, 220, 255, 200)
                        : IM_COL32(255, 180, 180, 200);
        case Judge::GOOD:
            return fast ? IM_COL32( 70, 150, 255, 200)
                        : IM_COL32(255, 120, 120, 200);
        case Judge::BAD:
            return fast ? IM_COL32(  0,  80, 255, 200)
                        : IM_COL32(255,  40,  40, 200);
        case Judge::POOR:
            return IM_COL32(180,  80, 255, 200);
        default:
            return COL_REPLAY;
    }
}

void ChartView::set_data(const Timeline* timeline, const ReplayData* replay) {
    timeline_ = timeline;
    replay_   = replay;

    for (int i = 0; i < 8; ++i) bms_lane_to_display_[i] = i;

    if (timeline_ && replay_) {
        // Auto-fallback: if selected player has no random info but the other does, switch
        if (!replay_->has_random_info[player_idx_] && replay_->has_random_info[1 - player_idx_]) {
            player_idx_ = 1 - player_idx_;
        }

        judge_engine_.set_player_index(player_idx_);

        // Mapping: only needs replay metadata (NOT hits)
        judge_engine_.compute_lane_mappings(*replay_);
        const int* b2d = judge_engine_.bms_to_display();
        for (int i = 0; i < 8; ++i) bms_lane_to_display_[i] = b2d[i];

        // Judgement: needs hits
        if (!replay_->hits.empty()) {
            judge_engine_.analyze(*timeline_, *replay_);
        }
    }
}

float ChartView::screen_y(tick_t tick, const ImVec2& win_pos, const ImVec2& win_size) const {
    float jy = win_pos.y + win_size.y - kPaddingBottom;
    float dt = static_cast<float>(tick) - static_cast<float>(current_tick_);
    return jy - dt * static_cast<float>(pixels_per_tick_);
}

void ChartView::handle_input() {
    if (ImGui::IsKeyPressed(ImGuiKey_Space) && !ImGui::IsAnyItemActive()) {
        is_playing_ = !is_playing_;
    }

    if (!ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows)) return;

    float wheel = ImGui::GetIO().MouseWheel;
    if (wheel == 0.0f) return;

    if (ImGui::GetIO().KeyCtrl) {
        pixels_per_tick_ = std::max(0.005, std::min(2.0,
            pixels_per_tick_ * (1.0 + static_cast<double>(wheel) * 0.12)));
    } else {
        current_tick_ -= static_cast<double>(wheel) * static_cast<double>(scroll_distance_);
    }

    if (timeline_) {
        tick_t t_end = timeline_->tick_end();
        current_tick_ = std::max(0.0, std::min(static_cast<double>(t_end), current_tick_));
        current_time_sec_ = timeline_->time_map.tick_to_second(
            static_cast<tick_t>(current_tick_));
    }
}

void ChartView::draw_background(ImDrawList* dl, const ImVec2& win_pos,
                                 const ImVec2& win_size) {
    dl->AddRectFilled(win_pos,
                      ImVec2(win_pos.x + win_size.x, win_pos.y + win_size.y),
                      COL_BG_FULL);

    tick_t min_tick = static_cast<tick_t>(current_tick_ - kPaddingBottom / pixels_per_tick_);
    tick_t max_tick = static_cast<tick_t>(current_tick_ +
                       (win_size.y - kPaddingBottom) / pixels_per_tick_);
    if (min_tick < 0) min_tick = 0;

    tick_t m_begin = min_tick / kMeasureTick;
    tick_t m_end   = (max_tick / kMeasureTick) + 1;

    float x0 = win_pos.x + chart_x0();
    float x1 = x0 + chart_width();

    for (tick_t m = m_begin; m <= m_end; ++m) {
        ImU32 bg = (m % 2 == 0) ? COL_BG_A : COL_BG_B;
        float y0 = screen_y(m * kMeasureTick + kMeasureTick, win_pos, win_size);
        float y1 = screen_y(m * kMeasureTick, win_pos, win_size);
        dl->AddRectFilled(ImVec2(x0, y1), ImVec2(x1, y0), bg);
    }
}

void ChartView::draw_grid_and_measures(ImDrawList* dl, const ImVec2& win_pos,
                                        const ImVec2& win_size) {
    tick_t min_tick = static_cast<tick_t>(current_tick_ - kPaddingBottom / pixels_per_tick_);
    tick_t max_tick = static_cast<tick_t>(current_tick_ +
                       (win_size.y - kPaddingBottom) / pixels_per_tick_);
    if (min_tick < 0) min_tick = 0;

    float x0 = win_pos.x + chart_x0();
    float x1 = x0 + chart_width();
    float label_x = x1 + 6.0f;

    tick_t t_begin = (min_tick / kMeasureTick) * kMeasureTick;
    tick_t t_end   = (max_tick / kBeatTick) * kBeatTick + kBeatTick;

    for (tick_t t = t_begin; t <= t_end; t += kBeatTick) {
        bool is_measure = (t % kMeasureTick == 0);
        float y = screen_y(t, win_pos, win_size);

        if (is_measure) {
            dl->AddLine(ImVec2(x0, y), ImVec2(x1, y), COL_MEASURE, 2.0f);
            int mnum = static_cast<int>(t / kMeasureTick);
            char buf[16];
            std::snprintf(buf, sizeof(buf), "%d", mnum);
            dl->AddText(ImVec2(label_x, y - 8.0f), COL_MEASURE_NUM, buf);
        } else {
            bool is_4th = (t % (kBeatTick * 4)) == 0;
            bool is_8th = (t % (kBeatTick * 2)) == 0;
            ImU32 col = COL_GRID_16;
            float thk = 0.5f;
            if (is_4th) { col = COL_GRID_4; thk = 1.0f; }
            else if (is_8th) { col = COL_GRID_8; thk = 0.7f; }
            dl->AddLine(ImVec2(x0, y), ImVec2(x1, y), col, thk);
        }
    }

    for (int lane = 0; lane <= kNumLanes; ++lane) {
        float lx = win_pos.x + chart_x0() + lane * kLaneWidth;
        float thk = (lane == 0 || lane == kNumLanes) ? 2.0f : 1.0f;
        dl->AddLine(ImVec2(lx, win_pos.y),
                    ImVec2(lx, win_pos.y + win_size.y), COL_LANE, thk);
    }

    float jy = win_pos.y + win_size.y - kPaddingBottom;
    dl->AddLine(ImVec2(x0, jy), ImVec2(x1, jy), COL_JUDGMENT, 1.5f);
}

void ChartView::draw_notes(ImDrawList* dl, const ImVec2& win_pos,
                            const ImVec2& win_size) {
    if (!timeline_) return;

    tick_t min_tick = static_cast<tick_t>(current_tick_ - kPaddingBottom / pixels_per_tick_);
    tick_t max_tick = static_cast<tick_t>(current_tick_ +
                       (win_size.y - kPaddingBottom) / pixels_per_tick_);
    if (min_tick < 0) min_tick = 0;

    for (size_t i = 0; i < timeline_->notes.size(); ++i) {
        const auto& n = timeline_->notes[i];

        if (n.end_tick < min_tick && n.tick < min_tick) continue;
        if (n.tick > max_tick) continue;

        int render_lane = bms_lane_to_display_[n.lane];
        if (render_lane >= kNumLanes) continue;
        int column = lane_to_column(render_lane, config.is_2p_layout);

        ImU32 color;
        switch (n.lane) {
            case 0: color = COL_SCRATCH; break;
            case 1: case 3: case 5: case 7:
                color = COL_WHITE; break;
            default:
                color = COL_BLUE; break;
        }

        float cx = win_pos.x + chart_x0() + column * kLaneWidth
                   + (kLaneWidth - kNoteWidth) * 0.5f;
        float cy = screen_y(n.tick, win_pos, win_size);

        float nh = std::max(kNoteMinHeight, note_thickness_);
        float ny = cy - nh * 0.5f;
        dl->AddRectFilled(ImVec2(cx, ny),
                          ImVec2(cx + kNoteWidth, ny + nh), color);

        if (n.end_tick > n.tick) {
            float bw = std::max(2.0f, note_thickness_ * 0.5f);
            float body_x  = cx + (kNoteWidth - bw) * 0.5f;
            float tail_y  = screen_y(n.end_tick, win_pos, win_size);
            float body_y0 = cy - nh * 0.5f + nh * 0.5f;
            float body_y1 = tail_y;

            float vis_top = screen_y(max_tick, win_pos, win_size);
            float vis_bot = screen_y(min_tick, win_pos, win_size);
            if (body_y1 > vis_bot) body_y1 = vis_bot;
            if (body_y0 < vis_top) body_y0 = vis_top;

            if (body_y1 < body_y0) {
                dl->AddRectFilled(ImVec2(body_x, body_y1),
                                  ImVec2(body_x + bw, body_y0), color);
            }

            float th = std::max(1.5f, note_thickness_ * 0.5f);
            float tail_mark_y = tail_y - th * 0.5f;
            if (tail_mark_y < vis_top) tail_mark_y = vis_top;
            if (tail_mark_y + th > vis_bot) tail_mark_y = vis_bot - th;
            dl->AddRectFilled(ImVec2(cx, tail_mark_y),
                              ImVec2(cx + kNoteWidth, tail_mark_y + th), COL_LN_TAIL);
        }
    }
}

void ChartView::draw_replay_hits(ImDrawList* dl, const ImVec2& win_pos,
                                  const ImVec2& win_size) {
    if (!config.show_replay || !replay_ || !timeline_) return;

    tick_t min_tick = static_cast<tick_t>(current_tick_ - kPaddingBottom / pixels_per_tick_);
    tick_t max_tick = static_cast<tick_t>(current_tick_ +
                       (win_size.y - kPaddingBottom) / pixels_per_tick_);
    if (min_tick < 0) min_tick = 0;

    float ppt_f = static_cast<float>(pixels_per_tick_);
    float line_thk = std::max(1.0f, note_thickness_ / 5.0f);

    // Release 标记: 菱形钻石。位置由调用者决定 (LR2 release hit 用 tick_start,
    // BRD 有 hold 时长的 press hit 用 tick_end)。
    static constexpr ImU32 COL_RELEASE = IM_COL32(170, 170, 220, 220);
    auto draw_release_diamond = [&](int column, tick_t tick) {
        if (tick < min_tick || tick > max_tick) return;
        float cx = win_pos.x + chart_x0()
                   + column * kLaneWidth + kLaneWidth * 0.5f;
        float cy = screen_y(tick, win_pos, win_size);
        float sz = std::max(2.5f, note_thickness_ / 3.0f);
        dl->AddQuadFilled(
            ImVec2(cx, cy - sz), ImVec2(cx + sz, cy),
            ImVec2(cx, cy + sz), ImVec2(cx - sz, cy),
            COL_RELEASE);
    };

    for (size_t i = 0; i < replay_->hits.size(); ++i) {
        const auto& hit = replay_->hits[i];

        if (!show_releases_ && !hit.is_press) continue;
        if (hit.tick_end < min_tick && hit.tick_start < min_tick) continue;
        if (hit.tick_start > max_tick) continue;
        if (hit.lane >= static_cast<uint8_t>(kNumLanes)) continue;

        int column = lane_to_column(hit.lane, config.is_2p_layout);
        tick_t t_start = hit.tick_start;

        ImU32 box_color = COL_REPLAY;
        int offset = 0;
        bool is_fast = false, is_slow = false;
        Judge jg = Judge::POOR;

        if (i < judge_engine_.results().size()) {
            const auto& hr = judge_engine_.results()[i];
            jg       = hr.judge;
            offset   = hr.offset_ms;
            is_fast  = hr.fast;
            is_slow  = hr.slow;
            box_color = judge_box_color(jg, is_fast);
        }

        // LR2: 每个 release 是独立 hit (is_press=false)。无论显示模式都画钻石。
        if (!hit.is_press) {
            draw_release_diamond(column, t_start);
            continue;
        }

        if (replay_display_mode_ == ReplayDisplayMode::LineOnly) {
            float lx0 = win_pos.x + chart_x0() + column * kLaneWidth;
            float lx1 = lx0 + kLaneWidth;
            float ly = screen_y(t_start, win_pos, win_size);
            dl->AddLine(ImVec2(lx0 + 2.0f, ly), ImVec2(lx1 - 2.0f, ly),
                        box_color, line_thk);
        } else if (replay_display_mode_ == ReplayDisplayMode::Marker) {
            float cx = win_pos.x + chart_x0() + column * kLaneWidth + kLaneWidth * 0.5f;
            float cy = screen_y(t_start, win_pos, win_size);
            float sz = std::max(2.0f, note_thickness_ / 3.0f);
            dl->AddCircleFilled(ImVec2(cx, cy), sz, box_color);
        } else {
            tick_t t_end = (hit.tick_end > hit.tick_start) ? hit.tick_end
                                                            : (hit.tick_start + 1);
            float sy0 = screen_y(t_start, win_pos, win_size);
            float sy1 = screen_y(t_end, win_pos, win_size);

            float vis_top = screen_y(max_tick, win_pos, win_size);
            float vis_bot = screen_y(min_tick, win_pos, win_size);
            if (sy1 > vis_bot) sy1 = vis_bot;
            if (sy0 < vis_top) sy0 = vis_top;

            float box_h = sy0 - sy1;
            float box_min_h = std::max(2.0f, ppt_f * 4.0f);
            if (box_h < box_min_h) box_h = box_min_h;

            float bx = win_pos.x + chart_x0() + column * kLaneWidth
                       + (kLaneWidth - kReplayBoxWidth) * 0.5f;
            float by = sy1;

            dl->AddRect(ImVec2(bx, by),
                        ImVec2(bx + kReplayBoxWidth, by + box_h),
                        box_color, 0.0f, 0, line_thk);
        }

        // BRD: press/release 被合并为单个 press hit, release 编码为 tick_end。
        // 在 line/marker 模式下 press 没有视觉范围, 需要在 tick_end 处补一个
        // release 钻石标记 (box 模式下 box 底边已隐含 release, 不重复绘制)。
        if (show_releases_ && hit.is_press
            && hit.tick_end > hit.tick_start
            && replay_display_mode_ != ReplayDisplayMode::FullHold) {
            draw_release_diamond(column, hit.tick_end);
        }

        // BRD hold-press: release 编码为 press hit 的 tick_end (is_press=true 且 tick_end>tick_start)。
        // Line/Marker 模式只画了按下位置, 这里补画松开位置的菱形; Box 模式已用矩形
        // 完整展示 hold 区间, 不再额外标记, 保持现有行为不变。
        if (show_releases_ && hit.is_press && hit.tick_end > hit.tick_start &&
            replay_display_mode_ != ReplayDisplayMode::FullHold) {
            draw_release_diamond(column, hit.tick_end);
        }

        // 新增: 用 show_fs_labels_ 控制是否绘制 F/S 标签文字
        if (show_fs_labels_ && (is_fast || is_slow) && jg != Judge::PGREAT) {
            char buf[16];
            std::snprintf(buf, sizeof(buf), "%c%d",
                          is_fast ? 'F' : 'S', std::abs(offset));
            ImU32 label_col = is_fast
                ? IM_COL32(128, 220, 255, 220)
                : IM_COL32(255, 180, 180, 220);
            float lx1 = win_pos.x + chart_x0() + (column + 1) * kLaneWidth;
            float ly = screen_y(t_start, win_pos, win_size);
            dl->AddText(ImVec2(lx1 + 3.0f, ly - 8.0f), label_col, buf);
        }
    }
}

void ChartView::draw_miss_notes(ImDrawList* dl, const ImVec2& win_pos,
                                 const ImVec2& win_size) {
    if (!config.show_replay || !timeline_ || !replay_ || replay_->hits.empty()) return;

    tick_t min_tick = static_cast<tick_t>(current_tick_ - kPaddingBottom / pixels_per_tick_);
    tick_t max_tick = static_cast<tick_t>(current_tick_ +
                       (win_size.y - kPaddingBottom) / pixels_per_tick_);
    if (min_tick < 0) min_tick = 0;

    const auto& missed = judge_engine_.missed_notes();
    static constexpr ImU32 COL_MISS = IM_COL32(180, 80, 255, 220);
    float miss_thk = std::max(1.0f, note_thickness_ / 5.0f);

    for (size_t i = 0; i < missed.size(); ++i) {
        const auto* n = missed[i];
        if (n->tick < min_tick) continue;
        if (n->tick > max_tick) continue;

        int render_lane = bms_lane_to_display_[n->lane];
        if (render_lane >= kNumLanes) continue;
        int column = lane_to_column(render_lane, config.is_2p_layout);

        float cx = win_pos.x + chart_x0() + column * kLaneWidth
                   + (kLaneWidth - kNoteWidth) * 0.5f;
        float cy = screen_y(n->tick, win_pos, win_size);
        float nh = std::max(kNoteMinHeight, note_thickness_);
        float ny = cy - nh * 0.5f;

        dl->AddRect(ImVec2(cx, ny),
                    ImVec2(cx + kNoteWidth, ny + nh),
                    COL_MISS, 0.0f, 0, miss_thk);
    }
}

void ChartView::render_controls_inline() {
    if (ImGui::Button(is_playing_ ? "Pause" : "Play")) {
        is_playing_ = !is_playing_;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(Space)");

    if (timeline_) {
        double total_sec = timeline_->time_map.tick_to_second(timeline_->tick_end());
        ImGui::Text("Time: %.2f / %.1f s", current_time_sec_, total_sec);
    }

    if (timeline_ && replay_ && !replay_->hits.empty()) {
        ImGui::Spacing();
        ImGui::SeparatorText("Accuracy");
        const auto& s = judge_engine_.statistics();
        ImGui::Text("PGREAT: %d", s.pgreat);
        ImGui::Text("GREAT:  %d", s.great);
        ImGui::Text("GOOD:   %d", s.good);
        ImGui::Text("BAD:    %d", s.bad);
        ImGui::Text("POOR:   %d", s.poor);
        ImGui::Separator();
        ImGui::Text("FAST: %d  |  SLOW: %d", s.fast, s.slow);
        ImGui::Text("Offset: %.1f +/- %.1f ms",
                    s.mean_offset, s.stddev_offset);
    }

    ImGui::SeparatorText("View");
    ImGui::Checkbox("Show Replay", &config.show_replay);

    int layout = config.is_2p_layout ? 2 : 1;
    ImGui::RadioButton("1P (SC left)",  &layout, 1);
    ImGui::SameLine();
    ImGui::RadioButton("2P (SC right)", &layout, 2);
    config.is_2p_layout = (layout == 2);

    ImGui::SeparatorText("Replay Display");
    int rdm = static_cast<int>(replay_display_mode_);
    ImGui::RadioButton("Line", &rdm, 0);
    ImGui::SameLine();
    ImGui::RadioButton("Box", &rdm, 1);
    ImGui::SameLine();
    ImGui::RadioButton("Marker", &rdm, 2);
    replay_display_mode_ = static_cast<ReplayDisplayMode>(rdm);
    ImGui::Checkbox("Show Releases", &show_releases_);

    ImGui::SeparatorText("Appearance");
    ImGui::SliderFloat("Note Speed", &note_speed_, 0.5f, 2.0f, "%.2fx");
    ImGui::SliderFloat("Note Thickness", &note_thickness_, 1.0f, 20.0f, "%.0f");
    static const int kScrollTickValues[] = { 3840, 7680, 11520, 15360 };
    static const char* kScrollLabels[]    = { "0.5 measure", "1 measure", "1.5 measures", "2 measures" };
    int scroll_idx = 0;
    for (int i = 0; i < 4; ++i) {
        if (scroll_distance_ >= kScrollTickValues[i]) scroll_idx = i;
    }
    if (ImGui::Combo("Scroll Distance", &scroll_idx, kScrollLabels, IM_ARRAYSIZE(kScrollLabels))) {
        scroll_distance_ = kScrollTickValues[scroll_idx];
    }
    ImGui::Checkbox("Auto Follow Playback", &auto_follow_playback_);

    ImGui::SeparatorText("Judge System");
    int js = (judge_engine_.system() == JudgeSystem::LR2) ? 0 : 1;
    if (ImGui::RadioButton("LR2", &js, 0)) {
        judge_engine_.set_system(JudgeSystem::LR2);
        if (timeline_ && replay_ && !replay_->hits.empty())
            judge_engine_.analyze(*timeline_, *replay_);
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("beatoraja", &js, 1)) {
        judge_engine_.set_system(JudgeSystem::Beatoraja);
        if (timeline_ && replay_ && !replay_->hits.empty())
            judge_engine_.analyze(*timeline_, *replay_);
    }

    ImGui::Separator();
    ImGui::TextDisabled("Scroll: Wheel  |  Zoom: Ctrl+Wheel");
    if (timeline_) {
        ImGui::Text("Notes: %zu  |  Hits: %zu",
                    timeline_->notes.size(),
                    replay_ ? replay_->hits.size() : 0);
    }

    ImGui::SeparatorText("Developer");
    ImGui::Checkbox("Show Developer Options", &show_dev_options);
    if (show_dev_options) {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.2f, 1.0f),
                           "WARNING: These options are for advanced users only.");

        if (timeline_ && replay_ && !replay_->hits.empty()) {
            ImGui::Spacing();
            ImGui::SeparatorText("Replay");
            const char* fmt_str = (replay_->format == ReplayFormat::LR2REP) ? "LR2REP" : "BRD";
            ImGui::Text("Format: %s", fmt_str);
            if (replay_->has_random_info[player_idx_]) {
                const char* mode_str = "OFF";
                if (replay_->random_mode[player_idx_] == LR2RandomMode::Mirror)  mode_str = "MIRROR";
                if (replay_->random_mode[player_idx_] == LR2RandomMode::Random)  mode_str = "RANDOM";
                if (replay_->random_mode[player_idx_] == LR2RandomMode::SRandom) mode_str = "S-RANDOM";
                if (replay_->random_mode[player_idx_] == LR2RandomMode::RRandom) mode_str = "R-RANDOM";
                ImGui::Text("Mode: %s", mode_str);
                ImGui::Text("Seed: %d", replay_->random_seed);
            }
            int prev = player_idx_;
            ImGui::RadioButton("P1", &player_idx_, 0); ImGui::SameLine();
            ImGui::RadioButton("P2", &player_idx_, 1);
            if (player_idx_ != prev && timeline_ && !replay_->hits.empty()) {
                set_data(timeline_, replay_);
            }
        }

        ImGui::Checkbox("##HashVerify", &hash_verify_enabled);
        ImGui::SameLine();
        ImGui::Text("Hash Verification");
        if (!hash_verify_status.empty()) {
            bool ok = hash_verify_status == "OK";
            ImGui::TextColored(ok ? ImVec4(0.2f, 1.0f, 0.3f, 1.0f)
                                  : ImVec4(1.0f, 0.3f, 0.2f, 1.0f),
                               "  %s", hash_verify_status.c_str());
        }

        ImGui::Checkbox("Show Debug Overlay", &show_debug_overlay);

        if (timeline_) {
            int r = timeline_->rank;
            if (r < 0) r = 0; if (r > 3) r = 3;
            auto rank = static_cast<JudgeRank>(r);
            const auto& w = JudgeProfile::get_window(judge_engine_.system(), rank);
            ImGui::SeparatorText("Judge Config");
            ImGui::Text("System: %s", judge_engine_.system() == JudgeSystem::LR2 ? "LR2" : "beatoraja");
            ImGui::Text("Rank:   %s (%d)", JudgeProfile::rank_string(rank), r);
            ImGui::Text("PG: %d ms  GR: %d ms  GD: %d ms", w.pg, w.gr, w.gd);
            ImGui::Text("BD: %d ms  POOR: %d ms", w.bd, w.poor);
        }
    }
}

void ChartView::render_controls_child() {
    render_controls_inline();
}

// 新增: 左栏 - 分析面板 (谱面元数据 + 时序偏移分布直方图 + 判定统计)
void ChartView::render_analysis_panel() {
    // 谱面元数据区块 (即使没有 replay 也显示)
    if (timeline_) {
        ImGui::SeparatorText("Chart Metadata");
        ImGui::TextWrapped("Title:  %s", timeline_->title.c_str());
        ImGui::TextWrapped("Artist: %s", timeline_->artist.c_str());
        ImGui::Text("BPM:    %.1f", timeline_->initial_bpm);
        if (!chart_sha256_.empty())
            ImGui::TextWrapped("SHA256: %s", chart_sha256_.c_str());
        if (!chart_md5_.empty())
            ImGui::TextWrapped("MD5:    %s", chart_md5_.c_str());
        if (!hash_verify_status.empty()) {
            bool ok = (hash_verify_status == "OK");
            ImGui::TextColored(ok ? ImVec4(0.4f, 1.0f, 0.4f, 1.0f)
                                  : ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
                               "Hash: %s", hash_verify_status.c_str());
        }
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
    }

    if (!timeline_ || !replay_ || replay_->hits.empty()) {
        ImGui::TextDisabled("No replay loaded.");
        return;
    }

    const auto& results = judge_engine_.results();
    if (results.empty()) {
        ImGui::TextDisabled("No judgement data.");
        return;
    }

    // 时序偏移分布直方图
    ImGui::SeparatorText("Timing Distribution");
    {
        constexpr int kBucketCount = 40;
        constexpr float kRangeMin = -100.0f;
        constexpr float kRangeMax = 100.0f;
        constexpr float kBucketWidth = (kRangeMax - kRangeMin) / kBucketCount;
        int buckets[kBucketCount] = {};

        for (const auto& hr : results) {
            if (hr.is_release) continue;
            float offset = static_cast<float>(hr.offset_ms);
            int idx = static_cast<int>((offset - kRangeMin) / kBucketWidth);
            if (idx >= 0 && idx < kBucketCount) buckets[idx]++;
        }

        int max_count = 1;
        for (int i = 0; i < kBucketCount; ++i)
            if (buckets[i] > max_count) max_count = buckets[i];

        ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
        float canvas_w = ImGui::GetContentRegionAvail().x;
        float canvas_h = 120.0f;
        ImDrawList* dl = ImGui::GetWindowDrawList();

        // 背景
        dl->AddRectFilled(canvas_pos,
            ImVec2(canvas_pos.x + canvas_w, canvas_pos.y + canvas_h),
            IM_COL32(20, 20, 40, 255));

        // 零线
        float zero_x = canvas_pos.x + canvas_w * (-kRangeMin) / (kRangeMax - kRangeMin);
        dl->AddLine(ImVec2(zero_x, canvas_pos.y),
                    ImVec2(zero_x, canvas_pos.y + canvas_h),
                    IM_COL32(120, 120, 160, 200), 1.0f);

        // 柱状图
        float bar_w = canvas_w / kBucketCount;
        for (int i = 0; i < kBucketCount; ++i) {
            float bar_h = (static_cast<float>(buckets[i]) / max_count) * (canvas_h - 4.0f);
            float bx = canvas_pos.x + i * bar_w;
            float by = canvas_pos.y + canvas_h - bar_h;
            ImU32 col = (i < kBucketCount / 2)
                ? IM_COL32(128, 220, 255, 180)   // FAST 侧 (蓝色)
                : IM_COL32(255, 180, 180, 180);   // SLOW 侧 (红色)
            dl->AddRectFilled(ImVec2(bx, by),
                              ImVec2(bx + bar_w - 1.0f, canvas_pos.y + canvas_h),
                              col);
        }

        // 标签
        dl->AddText(ImVec2(canvas_pos.x + 2, canvas_pos.y + 2),
                    IM_COL32(128, 220, 255, 200), "FAST");
        dl->AddText(ImVec2(canvas_pos.x + canvas_w - 36, canvas_pos.y + 2),
                    IM_COL32(255, 180, 180, 200), "SLOW");

        ImGui::Dummy(ImVec2(canvas_w, canvas_h));

        // 刻度标签
        ImGui::TextDisabled("-100ms");
        ImGui::SameLine(canvas_w * 0.5f - 4.0f);
        ImGui::TextDisabled("0");
        ImGui::SameLine(canvas_w - 36.0f);
        ImGui::TextDisabled("+100ms");
    }

    ImGui::Spacing();

    // 判定统计面板
    ImGui::SeparatorText("Judgement Stats");
    {
        const auto& s = judge_engine_.statistics();

        ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "PGREAT: %d", s.pgreat);
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "GREAT:  %d", s.great);
        ImGui::TextColored(ImVec4(0.3f, 0.6f, 1.0f, 1.0f), "GOOD:   %d", s.good);
        ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "BAD:    %d", s.bad);
        ImGui::TextColored(ImVec4(0.7f, 0.3f, 1.0f, 1.0f), "POOR:   %d", s.poor);

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::TextColored(ImVec4(0.5f, 0.85f, 1.0f, 1.0f), "FAST: %d", s.fast);
        ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.7f, 1.0f), "SLOW: %d", s.slow);

        ImGui::Spacing();
        ImGui::Text("Mean:   %.1f ms", s.mean_offset);
        ImGui::Text("StdDev: %.1f ms", s.stddev_offset);
    }
}

// 新增: 右栏 - 设置面板 (BASIC + 样式)
void ChartView::render_settings_panel() {
    // BASIC 分组
    ImGui::SeparatorText("BASIC");

    ImGui::SliderFloat("Note Speed", &note_speed_, 0.5f, 2.0f, "%.2fx");

    float cs = chart_speed();
    if (ImGui::SliderFloat("Chart Speed", &cs, 0.01f, 1.0f, "%.3f")) {
        set_chart_speed(cs);
    }

    int layout = config.is_2p_layout ? 2 : 1;
    ImGui::RadioButton("1P", &layout, 1);
    ImGui::SameLine();
    ImGui::RadioButton("2P", &layout, 2);
    config.is_2p_layout = (layout == 2);

    ImGui::Spacing();

    // 样式 分组
    ImGui::SeparatorText("Style");

    int rdm = static_cast<int>(replay_display_mode_);
    ImGui::RadioButton("Line", &rdm, 0);
    ImGui::SameLine();
    ImGui::RadioButton("Box", &rdm, 1);
    ImGui::SameLine();
    ImGui::RadioButton("Marker", &rdm, 2);
    replay_display_mode_ = static_cast<ReplayDisplayMode>(rdm);

    ImGui::SliderFloat("Note Thickness", &note_thickness_, 1.0f, 20.0f, "%.0f");

    ImGui::Checkbox("Show F/S Labels", &show_fs_labels_);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // 其他设置 (保留原有功能)
    ImGui::SeparatorText("View");
    ImGui::Checkbox("Show Replay", &config.show_replay);
    ImGui::Checkbox("Show Releases", &show_releases_);

    static const int kScrollTickValues[] = { 3840, 7680, 11520, 15360 };
    static const char* kScrollLabels[]    = { "0.5 measure", "1 measure", "1.5 measures", "2 measures" };
    int scroll_idx = 0;
    for (int i = 0; i < 4; ++i) {
        if (scroll_distance_ >= kScrollTickValues[i]) scroll_idx = i;
    }
    if (ImGui::Combo("Scroll Distance", &scroll_idx, kScrollLabels, IM_ARRAYSIZE(kScrollLabels))) {
        scroll_distance_ = kScrollTickValues[scroll_idx];
    }
    ImGui::Checkbox("Auto Follow Playback", &auto_follow_playback_);

    ImGui::SeparatorText("Judge System");
    int js = (judge_engine_.system() == JudgeSystem::LR2) ? 0 : 1;
    if (ImGui::RadioButton("LR2", &js, 0)) {
        judge_engine_.set_system(JudgeSystem::LR2);
        if (timeline_ && replay_ && !replay_->hits.empty())
            judge_engine_.analyze(*timeline_, *replay_);
    }
    ImGui::SameLine();
    if (ImGui::RadioButton("beatoraja", &js, 1)) {
        judge_engine_.set_system(JudgeSystem::Beatoraja);
        if (timeline_ && replay_ && !replay_->hits.empty())
            judge_engine_.analyze(*timeline_, *replay_);
    }

    ImGui::Separator();
    ImGui::TextDisabled("Scroll: Wheel  |  Zoom: Ctrl+Wheel");
    if (timeline_) {
        ImGui::Text("Notes: %zu  |  Hits: %zu",
                    timeline_->notes.size(),
                    replay_ ? replay_->hits.size() : 0);
    }

    ImGui::SeparatorText("Developer");
    ImGui::Checkbox("Show Developer Options", &show_dev_options);
    if (show_dev_options) {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.2f, 1.0f),
                           "WARNING: These options are for advanced users only.");

        if (timeline_ && replay_ && !replay_->hits.empty()) {
            ImGui::Spacing();
            ImGui::SeparatorText("Replay");
            const char* fmt_str = (replay_->format == ReplayFormat::LR2REP) ? "LR2REP" : "BRD";
            ImGui::Text("Format: %s", fmt_str);
            if (replay_->has_random_info[player_idx_]) {
                const char* mode_str = "OFF";
                if (replay_->random_mode[player_idx_] == LR2RandomMode::Mirror)  mode_str = "MIRROR";
                if (replay_->random_mode[player_idx_] == LR2RandomMode::Random)  mode_str = "RANDOM";
                if (replay_->random_mode[player_idx_] == LR2RandomMode::SRandom) mode_str = "S-RANDOM";
                if (replay_->random_mode[player_idx_] == LR2RandomMode::RRandom) mode_str = "R-RANDOM";
                ImGui::Text("Mode: %s", mode_str);
                ImGui::Text("Seed: %d", replay_->random_seed);
            }
            int prev = player_idx_;
            ImGui::RadioButton("P1", &player_idx_, 0); ImGui::SameLine();
            ImGui::RadioButton("P2", &player_idx_, 1);
            if (player_idx_ != prev && timeline_ && !replay_->hits.empty()) {
                set_data(timeline_, replay_);
            }
        }

        ImGui::Checkbox("##HashVerify", &hash_verify_enabled);
        ImGui::SameLine();
        ImGui::Text("Hash Verification");
        if (!hash_verify_status.empty()) {
            bool ok = hash_verify_status == "OK";
            ImGui::TextColored(ok ? ImVec4(0.2f, 1.0f, 0.3f, 1.0f)
                                  : ImVec4(1.0f, 0.3f, 0.2f, 1.0f),
                               "  %s", hash_verify_status.c_str());
        }

        ImGui::Checkbox("Show Debug Overlay", &show_debug_overlay);

        if (timeline_) {
            int r = timeline_->rank;
            if (r < 0) r = 0; if (r > 3) r = 3;
            auto rank = static_cast<JudgeRank>(r);
            const auto& w = JudgeProfile::get_window(judge_engine_.system(), rank);
            ImGui::SeparatorText("Judge Config");
            ImGui::Text("System: %s", judge_engine_.system() == JudgeSystem::LR2 ? "LR2" : "beatoraja");
            ImGui::Text("Rank:   %s (%d)", JudgeProfile::rank_string(rank), r);
            ImGui::Text("PG: %d ms  GR: %d ms  GD: %d ms", w.pg, w.gr, w.gd);
            ImGui::Text("BD: %d ms  POOR: %d ms", w.bd, w.poor);
        }
    }
}

// 新增: 底部密度图 - 显示每个 measure 的 note 数量
void ChartView::render_density_chart(ImDrawList* dl, const ImVec2& pos, const ImVec2& size) {
    if (timeline_ && !timeline_->notes.empty()) {
        tick_t t_end_tick = timeline_->tick_end();
        if (t_end_tick > 0 && ImGui::IsWindowHovered() && ImGui::IsMouseClicked(0)) {
            ImVec2 mp = ImGui::GetMousePos();
            float ml = 4.0f, mr = 4.0f;
            float cw = size.x - ml - mr;
            float rx = mp.x - pos.x - ml;
            float p = std::max(0.0f, std::min(1.0f, rx / cw));
            current_tick_ = static_cast<double>(p) * static_cast<double>(t_end_tick);
            current_time_sec_ = timeline_->time_map.tick_to_second(
                static_cast<tick_t>(current_tick_));
        }
    }

    // 背景
    dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y),
                      IM_COL32(20, 20, 40, 255));

    if (!timeline_ || timeline_->notes.empty()) {
        dl->AddText(ImVec2(pos.x + 4, pos.y + 4), IM_COL32(120, 120, 160, 200),
                    "Note Density");
        return;
    }

    // 统计每个 measure 的 note 数量
    tick_t t_end = timeline_->tick_end();
    int num_measures = static_cast<int>(t_end / kMeasureTick) + 1;
    if (num_measures <= 0) return;

    std::vector<int> density(num_measures, 0);
    for (const auto& n : timeline_->notes) {
        int m = static_cast<int>(n.tick / kMeasureTick);
        if (m >= 0 && m < num_measures) density[m]++;
    }

    int max_density = 1;
    for (int d : density) if (d > max_density) max_density = d;

    float margin_l = 4.0f;
    float margin_r = 4.0f;
    float margin_t = 14.0f;
    float margin_b = 4.0f;
    float chart_w = size.x - margin_l - margin_r;
    float chart_h = size.y - margin_t - margin_b;
    float bar_w = chart_w / num_measures;

    // 标题
    dl->AddText(ImVec2(pos.x + 4, pos.y + 2), IM_COL32(180, 180, 220, 200),
                "Note Density");

    // 绘制柱状图
    for (int i = 0; i < num_measures; ++i) {
        float bar_h = (static_cast<float>(density[i]) / max_density) * chart_h;
        float bx = pos.x + margin_l + i * bar_w;
        float by = pos.y + margin_t + chart_h - bar_h;

        ImU32 col = IM_COL32(80, 160, 255, 160);
        if (bar_w >= 2.0f) {
            dl->AddRectFilled(ImVec2(bx, by),
                              ImVec2(bx + bar_w - 0.5f, pos.y + margin_t + chart_h),
                              col);
        } else {
            dl->AddLine(ImVec2(bx, by),
                        ImVec2(bx, pos.y + margin_t + chart_h),
                        col, 1.0f);
        }
    }

    // 当前播放位置指示线
    if (t_end > 0) {
        float progress = static_cast<float>(current_tick_ / t_end);
        float indicator_x = pos.x + margin_l + progress * chart_w;
        dl->AddLine(ImVec2(indicator_x, pos.y + margin_t),
                    ImVec2(indicator_x, pos.y + margin_t + chart_h),
                    IM_COL32(255, 80, 80, 220), 1.5f);
    }

    ImVec2 mouse_pos = ImGui::GetMousePos();
    if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(0)) {
        float rel_x = mouse_pos.x - (pos.x + margin_l);

        if (rel_x >= 0 && rel_x <= chart_w && timeline_) {
            float progress = rel_x / chart_w;
            current_tick_ = static_cast<double>(progress * t_end);
            current_time_sec_ = timeline_->time_map.tick_to_second(static_cast<tick_t>(current_tick_));
        }
    }
}

void ChartView::render_analyzer() {
    if (is_playing_ && timeline_) {
        current_time_sec_ += ImGui::GetIO().DeltaTime * static_cast<double>(note_speed_);
        double new_tick = timeline_->time_map.second_to_tick(current_time_sec_);
        if (new_tick >= timeline_->tick_end()) {
            new_tick = static_cast<double>(timeline_->tick_end());
            is_playing_ = false;
        }
        if (auto_follow_playback_) {
            current_tick_ = new_tick;
        }
    }

    // 中栏: 谱面视图 (~80%) + 底部密度图 (~20%)
    float total_h = ImGui::GetContentRegionAvail().y;
    float button_row_h = 28.0f;
    float density_h = total_h * 0.18f;
    float chart_h = total_h - density_h - button_row_h - 8.0f;

    ImGui::BeginChild("ChartPanel", ImVec2(0, chart_h), true);
    {
        handle_input();

        ImVec2 win_pos  = ImGui::GetWindowPos();
        ImVec2 win_size = ImGui::GetWindowSize();
        ImDrawList* dl  = ImGui::GetWindowDrawList();

        ImVec2 content_min = ImGui::GetWindowContentRegionMin();
        ImVec2 content_max = ImGui::GetWindowContentRegionMax();
        dl->PushClipRect(ImVec2(win_pos.x + content_min.x, win_pos.y + content_min.y),
                         ImVec2(win_pos.x + content_max.x, win_pos.y + content_max.y),
                         true);

        draw_background(dl, win_pos, win_size);
        draw_grid_and_measures(dl, win_pos, win_size);
        draw_notes(dl, win_pos, win_size);
        draw_replay_hits(dl, win_pos, win_size);
        draw_miss_notes(dl, win_pos, win_size);

        dl->PopClipRect();

        if (show_debug_overlay && replay_) {
            auto mode_name = [](LR2RandomMode m) -> const char* {
                if (m == LR2RandomMode::Mirror)  return "MIRROR";
                if (m == LR2RandomMode::Random)  return "RANDOM";
                if (m == LR2RandomMode::SRandom) return "S-RANDOM";
                if (m == LR2RandomMode::RRandom) return "R-RANDOM";
                return "OFF";
            };
            ImGui::SetNextWindowPos(ImVec2(win_pos.x + content_min.x + 4,
                                           win_pos.y + content_min.y + 4));
            ImGui::SetNextWindowBgAlpha(0.55f);
            if (ImGui::Begin("##debug", nullptr,
                ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav |
                ImGuiWindowFlags_NoSavedSettings)) {
                ImGui::Text("Format:    %d  (%s)", replay_ ? (int)replay_->format : -1,
                    replay_ ? (replay_->format == ReplayFormat::LR2REP ? "LR2REP" : "BRD") : "N/A");
                if (replay_) {
                    ImGui::TextColored(player_idx_ == 0 ? ImVec4(0,1,0,1) : ImVec4(0.5f,0.5f,0.5f,1),
                        "[P1] HasRandom:%d  Mode:%d (%s)%s",
                        (int)replay_->has_random_info[0], (int)replay_->random_mode[0],
                        mode_name(replay_->random_mode[0]),
                        player_idx_ == 0 ? "  <- active" : "");
                    ImGui::TextColored(player_idx_ == 1 ? ImVec4(0,1,0,1) : ImVec4(0.5f,0.5f,0.5f,1),
                        "[P2] HasRandom:%d  Mode:%d (%s)%s",
                        (int)replay_->has_random_info[1], (int)replay_->random_mode[1],
                        mode_name(replay_->random_mode[1]),
                        player_idx_ == 1 ? "  <- active" : "");
                }
                ImGui::Text("Seed:      %d", replay_ ? replay_->random_seed : -1);
                ImGui::Text("Mapping:   [%d,%d,%d,%d,%d,%d,%d,%d]",
                    bms_lane_to_display_[0], bms_lane_to_display_[1],
                    bms_lane_to_display_[2], bms_lane_to_display_[3],
                    bms_lane_to_display_[4], bms_lane_to_display_[5],
                    bms_lane_to_display_[6], bms_lane_to_display_[7]);
            }
            ImGui::End();
        }

        ImGui::Separator();
        if (timeline_) {
            ImGui::Text("Tick: %.0f  |  Zoom: %.3f pt  |  %s",
                        current_tick_, pixels_per_tick_,
                        is_playing_ ? "PLAYING" : "PAUSED");
        }
    }
    ImGui::EndChild();

    // 新增: 底部密度图
    ImGui::BeginChild("DensityChart", ImVec2(0, density_h), true);
    {
        ImVec2 dpos  = ImGui::GetWindowPos();
        ImVec2 dsize = ImGui::GetWindowSize();
        ImDrawList* ddl = ImGui::GetWindowDrawList();
        render_density_chart(ddl, dpos, dsize);

    }
    ImGui::EndChild();

    {
        float avail_w = ImGui::GetContentRegionAvail().x;
        float btn_w = 60.0f;
        float indent = (avail_w - btn_w) * 0.5f;
        if (indent > 0.0f) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + indent);

        if (ImGui::Button(is_playing_ ? "||" : ">", ImVec2(btn_w, 0))) {
            is_playing_ = !is_playing_;
        }
    }
}

} // namespace bmv
