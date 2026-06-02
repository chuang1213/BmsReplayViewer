#include "judgement_engine.h"
#include "replay/lr2_random.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>
#include <map>

namespace bmv {

// ── LR2 timing windows (ms) ──
static constexpr int LR2_PGREAT[4] = { 8, 15, 18, 21 };
static constexpr int LR2_GREAT[4]  = {24, 30, 40, 60 };
static constexpr int LR2_GOOD[4]   = {40, 60, 100, 120};
static constexpr int LR2_BAD       = 200;

Judge JudgementEngine::classify_lr2(int offset_ms, int rank) const {
    if (rank < 0) rank = 0; if (rank > 3) rank = 3;
    int abs_ms = std::abs(offset_ms);
    if (abs_ms <= LR2_PGREAT[rank]) return Judge::PGREAT;
    if (abs_ms <= LR2_GREAT[rank])  return Judge::GREAT;
    if (abs_ms <= LR2_GOOD[rank])   return Judge::GOOD;
    if (abs_ms <= LR2_BAD)          return Judge::BAD;
    return Judge::POOR;
}

// ── beatoraja timing windows (ms) ──
static constexpr int BJA_PGREAT_BASE  = 20;
static constexpr int BJA_GREAT_BASE   = 60;
static constexpr int BJA_GOOD_BASE    = 150;
static constexpr int BJA_BAD_FAST_BASE = 220;
static constexpr int BJA_BAD_SLOW_BASE = 280;
static constexpr double BJA_SCALE[4]   = {0.25, 0.50, 0.75, 1.00};

Judge JudgementEngine::classify_beatoraja(int offset_ms, int rank) const {
    if (rank < 0) rank = 0; if (rank > 3) rank = 3;
    double s  = BJA_SCALE[rank];
    int pgreat = static_cast<int>(BJA_PGREAT_BASE * s);
    int great  = static_cast<int>(BJA_GREAT_BASE  * s);
    int good   = static_cast<int>(BJA_GOOD_BASE   * s);
    int bad_f  = static_cast<int>(BJA_BAD_FAST_BASE * s);
    int bad_s  = static_cast<int>(BJA_BAD_SLOW_BASE * s);

    int abs_ms = std::abs(offset_ms);
    if (abs_ms <= pgreat) return Judge::PGREAT;
    if (abs_ms <= great)  return Judge::GREAT;
    if (abs_ms <= good)   return Judge::GOOD;
    if (offset_ms < 0) { if (abs_ms <= bad_f) return Judge::BAD; }
    else               { if (offset_ms <= bad_s) return Judge::BAD; }
    return Judge::POOR;
}

static int max_judge_window_ms(JudgeSystem sys, int rank) {
    if (sys == JudgeSystem::LR2) return 200;
    if (rank < 0) rank = 0; if (rank > 3) rank = 3;
    double s = BJA_SCALE[rank];
    int bad_f = static_cast<int>(BJA_BAD_FAST_BASE * s);
    int bad_s = static_cast<int>(BJA_BAD_SLOW_BASE * s);
    return std::max(bad_f, bad_s);
}

void JudgementEngine::compute_lane_mappings(const ReplayData& replay) {
    for (int i = 0; i < 8; ++i) {
        computed_display_to_bms_[i] = i;
        computed_bms_to_display_[i] = i;
    }

    if (replay.format == ReplayFormat::BRD && replay.has_shuffle) {
        for (int i = 0; i < 8; ++i) {
            int original_val = replay.shuffle_pattern[i];
            int bms_lane     = (original_val == 7) ? 0 : (original_val + 1);
            int display_lane = (i == 7) ? 0 : (i + 1);
            computed_display_to_bms_[display_lane] = bms_lane;
            computed_bms_to_display_[bms_lane] = display_lane;
        }
    } else if (replay.format == ReplayFormat::LR2REP && replay.has_random_info[player_idx_]) {
        switch (replay.random_mode[player_idx_]) {
            case LR2RandomMode::Mirror:
                computed_display_to_bms_[1] = 7; computed_display_to_bms_[2] = 6;
                computed_display_to_bms_[3] = 5; computed_display_to_bms_[4] = 4;
                computed_display_to_bms_[5] = 3; computed_display_to_bms_[6] = 2;
                computed_display_to_bms_[7] = 1;
                computed_bms_to_display_[1] = 7; computed_bms_to_display_[2] = 6;
                computed_bms_to_display_[3] = 5; computed_bms_to_display_[4] = 4;
                computed_bms_to_display_[5] = 3; computed_bms_to_display_[6] = 2;
                computed_bms_to_display_[7] = 1;
                break;
            case LR2RandomMode::Random:
                build_random_lane_pattern(replay.random_seed, 7,
                    computed_display_to_bms_, computed_bms_to_display_);
                break;
            case LR2RandomMode::SRandom:
            case LR2RandomMode::RRandom:
                break;
            default:
                break;
        }
    }
}

// ── Main analysis (Note-driven Lane Cursor model) ──

void JudgementEngine::analyze(const Timeline& timeline,
                               const ReplayData& replay,
                               int rank) {
    results_.clear();
    missed_notes_.clear();
    results_.clear();

    compute_lane_mappings(replay);

    int* display_to_bms = computed_display_to_bms_;

    // → Build BMS-lane → sorted note indices + hit indices (press only)
    std::map<int, std::vector<size_t>> lane_notes;
    std::map<int, std::vector<size_t>> lane_hits;

    for (size_t i = 0; i < timeline.notes.size(); ++i) {
        const auto& n = timeline.notes[i];
        if (n.lane <= 7) lane_notes[n.lane].push_back(i);
    }
    for (auto& kv : lane_notes) {
        std::sort(kv.second.begin(), kv.second.end(),
            [&](size_t a, size_t b) {
                return timeline.notes[a].tick < timeline.notes[b].tick;
            });
    }

    for (size_t i = 0; i < replay.hits.size(); ++i) {
        if (!replay.hits[i].is_press) continue;
        int bms_lane = display_to_bms[replay.hits[i].lane];
        if (bms_lane >= 0 && bms_lane <= 7)
            lane_hits[bms_lane].push_back(i);
    }
    for (auto& kv : lane_hits) {
        std::sort(kv.second.begin(), kv.second.end(),
            [&](size_t a, size_t b) {
                return replay.hits[a].tick_start < replay.hits[b].tick_start;
            });
    }

    std::vector<bool> hit_used(replay.hits.size(), false);
    std::vector<bool> note_matched(timeline.notes.size(), false);

    stats_ = {};
    int max_win = max_judge_window_ms(system_, rank);
    std::vector<double> offsets_for_stats;
    offsets_for_stats.reserve(replay.hits.size());

    int matched_hits  = 0;
    int unmatched_hits = 0;
    int missed_notes_count = 0;

    // → Process each BMS lane independently
    for (auto& kv : lane_notes) {
        int bms_lane = kv.first;
        const auto& notes = kv.second;  // sorted by tick
        const auto& hits  = lane_hits.count(bms_lane) ? lane_hits[bms_lane]
                                                       : std::vector<size_t>{};

        size_t hi = 0;

        for (size_t ni = 0; ni < notes.size(); ++ni) {
            size_t note_idx = notes[ni];
            const auto& note = timeline.notes[note_idx];

            // Skip consumed hits
            while (hi < hits.size() && hit_used[hits[hi]]) hi++;

            bool matched = false;

            while (hi < hits.size()) {
                size_t hit_idx = hits[hi];
                const auto& hit = replay.hits[hit_idx];

                double sec_hit;
                if (replay.format == ReplayFormat::LR2REP)
                    sec_hit = hit.time_sec;
                else
                    sec_hit = timeline.time_map.tick_to_second(hit.tick_start);
                double sec_note = timeline.time_map.tick_to_second(note.tick);
                int offset_ms = static_cast<int>((sec_hit - sec_note) * 1000.0);

                if (offset_ms < -max_win) {
                    // Hit too early for this note AND all future notes
                    // → orphan POOR
                    HitResult hr;
                    hr.hit = &hit;
                    hr.judge = Judge::POOR;
                    hr.offset_ms = offset_ms;
                    hr.fast = true;
                    results_.push_back(hr);
                    stats_.poor++;
                    stats_.fast++;
                    hit_used[hit_idx] = true;
                    unmatched_hits++;
                    hi++;
                    continue;
                }

                if (std::abs(offset_ms) <= max_win) {
                    // → Match!
                    HitResult hr;
                    hr.hit  = &hit;
                    hr.note = &note;
                    hr.offset_ms = offset_ms;

                    if (offset_ms < 0) { hr.fast = true; stats_.fast++; }
                    else if (offset_ms > 0) { hr.slow = true; stats_.slow++; }

                    if (system_ == JudgeSystem::LR2)
                        hr.judge = classify_lr2(offset_ms, rank);
                    else
                        hr.judge = classify_beatoraja(offset_ms, rank);

                    results_.push_back(hr);
                    offsets_for_stats.push_back(static_cast<double>(offset_ms));
                    hit_used[hit_idx] = true;
                    note_matched[note_idx] = true;
                    matched_hits++;
                    hi++;
                    matched = true;
                    break;
                }

                // offset_ms > max_win: hit too late for this note
                // → note missed. Keep hi at current position; next note may match it.
                break;
            }

            if (!matched) {
                // Note was never matched → missed
                missed_notes_.push_back(&note);
                missed_notes_count++;
                // Only count normal notes (not LN) in POOR stat
                if (note.end_tick == note.tick) stats_.poor++;
            }
        }

        // Remaining unused hits for this lane → POOR
        while (hi < hits.size()) {
            size_t hit_idx = hits[hi];
            if (!hit_used[hit_idx]) {
                const auto& hit = replay.hits[hit_idx];
                HitResult hr;
                hr.hit = &hit;
                hr.judge = Judge::POOR;
                results_.push_back(hr);
                stats_.poor++;
                hit_used[hit_idx] = true;
                unmatched_hits++;
            }
            hi++;
        }
    }

    // → Accumulate stat counts from results_
    // (already counted during matching; this is just for aggregation)
    int pg = 0, gr = 0, gd = 0, bd = 0, pr = 0;
    int fst = 0, slw = 0;
    for (auto& r : results_) {
        if (r.is_release) continue;
        switch (r.judge) {
            case Judge::PGREAT: pg++; break;
            case Judge::GREAT:  gr++; break;
            case Judge::GOOD:   gd++; break;
            case Judge::BAD:    bd++; break;
            case Judge::POOR:   pr++; break;
        }
        if (r.fast) fst++;
        if (r.slow) slw++;
    }
    stats_.pgreat = pg;
    stats_.great  = gr;
    stats_.good   = gd;
    stats_.bad    = bd;
    stats_.poor   = pr;
    stats_.fast   = fst;
    stats_.slow   = slw;

    // → Compute mean & stddev
    if (!offsets_for_stats.empty()) {
        double sum = 0.0;
        for (double v : offsets_for_stats) sum += v;
        stats_.mean_offset = sum / offsets_for_stats.size();

        double sq_sum = 0.0;
        for (double v : offsets_for_stats) {
            double d = v - stats_.mean_offset;
            sq_sum += d * d;
        }
        stats_.stddev_offset = std::sqrt(sq_sum / offsets_for_stats.size());
    }

    // → Reorder results_ to match replay hit order for ChartView index lookup
    std::map<const ReplayHit*, size_t> result_for_hit;
    for (size_t i = 0; i < results_.size(); ++i) {
        if (results_[i].hit) result_for_hit[results_[i].hit] = i;
    }
    std::vector<HitResult> ordered(replay.hits.size());
    for (size_t i = 0; i < replay.hits.size(); ++i) {
        if (!replay.hits[i].is_press) {
            ordered[i].hit = &replay.hits[i];
            ordered[i].is_release = true;
            continue;
        }
        auto it = result_for_hit.find(&replay.hits[i]);
        if (it != result_for_hit.end()) {
            ordered[i] = results_[it->second];
        } else {
            ordered[i].hit = &replay.hits[i];
            ordered[i].judge = Judge::POOR;
        }
    }
    results_ = std::move(ordered);

    // → Judge Audit
    const char* sys_name = (system_ == JudgeSystem::LR2) ? "LR2" : "beatoraja";
    std::fprintf(stdout, "\n=== Judge Audit ===\n");
    std::fprintf(stdout, "System:          %s\n", sys_name);
    std::fprintf(stdout, "Rank:            %d\n", rank);
    std::fprintf(stdout, "Max Window (ms): %d\n", max_win);
    std::fprintf(stdout, "Replay Hits:     %zu\n", replay.hits.size());
    std::fprintf(stdout, "Matched Hits:    %d\n", matched_hits);
    std::fprintf(stdout, "Unmatched Hits:  %d\n", unmatched_hits);
    std::fprintf(stdout, "Missed Notes:    %d\n", missed_notes_count);
    std::fprintf(stdout, "Shuffle Active:  %s\n",
                 replay.has_shuffle ? "YES" : "NO");
    if (replay.format == ReplayFormat::LR2REP) {
        const char* mode = "OFF";
        if (replay.random_mode[player_idx_] == LR2RandomMode::Mirror)  mode = "MIRROR";
        if (replay.random_mode[player_idx_] == LR2RandomMode::Random)  mode = "RANDOM";
        if (replay.random_mode[player_idx_] == LR2RandomMode::SRandom) mode = "S-RANDOM";
        std::fprintf(stdout, "LR2 Random Mode: %s\n", mode);
        std::fprintf(stdout, "LR2 Random Seed: %d\n", replay.random_seed);
        std::fprintf(stdout, "LR2 op210 Count: %zu\n", replay.lr2_judgements.size());
    }
    std::fprintf(stdout, "Statistics:\n");
    std::fprintf(stdout, "  PGREAT: %d  GREAT: %d  GOOD: %d  BAD: %d  POOR: %d\n",
                 stats_.pgreat, stats_.great, stats_.good, stats_.bad, stats_.poor);
    std::fprintf(stdout, "  FAST: %d  SLOW: %d\n", stats_.fast, stats_.slow);
    if (!offsets_for_stats.empty()) {
        std::fprintf(stdout, "  Mean: %.1f ms  StdDev: %.1f ms\n",
                     stats_.mean_offset, stats_.stddev_offset);
    }
    std::fprintf(stdout, "===================\n\n");
}

const HitResult* JudgementEngine::result_for_hit(size_t hit_index) const {
    if (hit_index < results_.size()) return &results_[hit_index];
    return nullptr;
}

} // namespace bmv
