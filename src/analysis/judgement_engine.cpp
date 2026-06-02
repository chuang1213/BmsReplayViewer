#include "judgement_engine.h"
#include "replay/lr2_random.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>
#include <map>

namespace bmv {

// ── Diagnostic helpers ──
static const char* judge_name(Judge j) {
    switch (j) {
        case Judge::PGREAT: return "PGREAT";
        case Judge::GREAT:  return "GREAT";
        case Judge::GOOD:   return "GOOD";
        case Judge::BAD:    return "BAD";
        case Judge::POOR:   return "POOR";
    }
    return "?";
}

static uint8_t judge_to_lr2_val(Judge j) {
    switch (j) {
        case Judge::PGREAT: return 5;
        case Judge::GREAT:  return 4;
        case Judge::GOOD:   return 3;
        case Judge::BAD:    return 2;
        case Judge::POOR:   return 1;
    }
    return 0;
}

static const char* lr2_val_name(uint8_t v) {
    switch (v) {
        case 5: return "PGREAT";
        case 4: return "GREAT";
        case 3: return "GOOD";
        case 2: return "BAD";
        case 1: return "POOR";
        case 0: return "POOR/MISS";
        default: return "?";
    }
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
                               const ReplayData& replay) {
    results_.clear();
    missed_notes_.clear();

    compute_lane_mappings(replay);

    int rank_raw = timeline.rank;
    if (rank_raw < 0) rank_raw = 0; if (rank_raw > 3) rank_raw = 3;
    JudgeRank judge_rank = static_cast<JudgeRank>(rank_raw);
    const JudgeWindow& win = JudgeProfile::get_window(system_, judge_rank);

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

    std::vector<bool> note_matched(timeline.notes.size(), false);

    stats_ = {};
    std::vector<double> offsets_for_stats;
    offsets_for_stats.reserve(replay.hits.size());

    int matched_hits  = 0;
    int unmatched_hits = 0;
    int missed_notes_count = 0;

    // → Process each BMS lane independently (hit-driven with note cursor auto-advance)
    for (auto& kv : lane_notes) {
        int bms_lane = kv.first;
        const auto& notes = kv.second;  // sorted by tick
        const auto& hits  = lane_hits.count(bms_lane) ? lane_hits[bms_lane]
                                                        : std::vector<size_t>{};
        size_t si = 0;  // note cursor index within this lane's notes

        for (size_t hi = 0; hi < hits.size(); ++hi) {
            size_t hit_idx = hits[hi];
            const auto& hit = replay.hits[hit_idx];
            if (!hit.is_press) continue;  // releases handled in LN tail pass

            double sec_hit;
            if (replay.format == ReplayFormat::LR2REP)
                sec_hit = hit.time_sec;
            else
                sec_hit = timeline.time_map.tick_to_second(hit.tick_start);

            // 1. Auto-advance: mark way-past notes as POOR
            while (si < notes.size()) {
                size_t note_idx = notes[si];
                const auto& note = timeline.notes[note_idx];
                // Note's effective end: use end_tick for LN, tick for normal
                double sec_note_end = timeline.time_map.tick_to_second(
                    note.end_tick > note.tick ? note.end_tick : note.tick);
                // Note still within POOR reach: stop advancing
                if (sec_hit <= sec_note_end + static_cast<double>(win.poor) / 1000.0) break;

                // Note is way past → mark as missed (POOR)
                missed_notes_.push_back(&note);
                missed_notes_count++;
                si++;
            }

            // No more notes on this lane: remaining hits are orphan POOR
            if (si >= notes.size()) {
                HitResult hr;
                hr.hit = &hit;
                hr.judge = Judge::POOR;
                results_.push_back(hr);
                unmatched_hits++;
                continue;
            }

            // 2. Current note
            size_t note_idx = notes[si];
            const auto& note = timeline.notes[note_idx];
            double sec_note = timeline.time_map.tick_to_second(note.tick);
            double offset_ms = (sec_hit - sec_note) * 1000.0;

            // 2. LR2 recursive judgment
            int abs_ms = static_cast<int>(std::fabs(offset_ms));
            int max_bd = std::max(win.bd_fast, win.bd_slow);

            // Hit too early for this note → orphan POOR
            if (offset_ms < -max_bd) {
                HitResult hr;
                hr.hit = &hit;
                hr.judge = Judge::POOR;
                hr.offset_ms = offset_ms;
                hr.fast = true;
                results_.push_back(hr);
                unmatched_hits++;
                continue;
            }

            // Try to judge current note
            bool note_judged = false;
            if (abs_ms <= max_bd) {
                HitResult hr;
                hr.hit  = &hit;
                hr.note = &note;
                hr.offset_ms = offset_ms;

                if (offset_ms < 0) { hr.fast = true; }
                else if (offset_ms > 0) { hr.slow = true; }

                if (abs_ms <= win.pg)            hr.judge = Judge::PGREAT;
                else if (abs_ms <= win.gr)       hr.judge = Judge::GREAT;
                else if (abs_ms <= win.gd)       hr.judge = Judge::GOOD;
                else if (offset_ms < 0 && abs_ms <= win.bd_fast) hr.judge = Judge::BAD;
                else if (offset_ms >= 0 && abs_ms <= win.bd_slow) hr.judge = Judge::BAD;
                else                             hr.judge = Judge::POOR;

                results_.push_back(hr);
                offsets_for_stats.push_back(offset_ms);
                note_matched[note_idx] = true;
                matched_hits++;
                note_judged = true;
            }

            // Recursive: if next note also within BAD window, retry same hit
            if (si + 1 < notes.size()) {
                size_t next_note_idx = notes[si + 1];
                const auto& next_note = timeline.notes[next_note_idx];
                double sec_next = timeline.time_map.tick_to_second(next_note.tick);
                int next_offset = static_cast<int>((sec_hit - sec_next) * 1000.0);
                if (std::abs(next_offset) <= max_bd) {
                    si++;
                    hi--;
                    continue;
                }
            }

            // Note not judged → missed
            if (!note_judged) {
                missed_notes_.push_back(&note);
                missed_notes_count++;
            }
            si++;
        }

        // Remaining notes beyond last hit → missed
        while (si < notes.size()) {
            size_t note_idx = notes[si];
            const auto& note = timeline.notes[note_idx];
            missed_notes_.push_back(&note);
            missed_notes_count++;
            si++;
        }
    }

#ifdef BMV_DEBUG
    // ── Time axis drift check (lane hits ↔ lane notes alignment) ──
    {
        int time_align_printed = 0;
        constexpr int kMaxTimeAlignLines = 20;
        for (auto& kv : lane_notes) {
            int bms_lane = kv.first;
            const auto& notes = kv.second;
            if (!lane_hits.count(bms_lane)) continue;
            const auto& hits = lane_hits.at(bms_lane);

            for (size_t hi : hits) {
                const auto& hit = replay.hits[hi];
                double sec_hit;
                if (replay.format == ReplayFormat::LR2REP)
                    sec_hit = hit.time_sec;
                else
                    sec_hit = timeline.time_map.tick_to_second(hit.tick_start);

                tick_t hit_tick_from_sec = timeline.time_map.second_to_tick(sec_hit);

                double min_diff = 1e9;
                tick_t closest_tick = 0;
                for (size_t ni : notes) {
                    const auto& note = timeline.notes[ni];
                    double sec_note = timeline.time_map.tick_to_second(note.tick);
                    double diff = std::fabs(sec_hit - sec_note);
                    if (diff < min_diff) { min_diff = diff; closest_tick = note.tick; }
                }

                if (min_diff > 0.001 && time_align_printed < kMaxTimeAlignLines) {
                    std::fprintf(stdout,
                        "TimeAlign Lane%d hit[%zu]: sec_hit=%.6f → tick=%lld  "
                        "closest_note_tick=%lld  delta_tick=%lld  delta_s=%.6f\n",
                        bms_lane, hi, sec_hit, (long long)hit_tick_from_sec,
                        (long long)closest_tick,
                        (long long)(hit_tick_from_sec - closest_tick),
                        min_diff);
                    time_align_printed++;
                }
            }
        }
    }
#endif

    // ── LN tail processing: find release events for matched LN notes ──
    // Build a map from note index to its head HitResult
    std::map<size_t, HitResult*> note_head_result;
    for (auto& r : results_) {
        if (!r.is_release && r.note) {
            size_t note_idx = static_cast<size_t>(r.note - &timeline.notes[0]);
            note_head_result[note_idx] = &r;
        }
    }

    std::vector<bool> release_used(replay.hits.size(), false);

    for (size_t i = 0; i < timeline.notes.size(); ++i) {
        const auto& note = timeline.notes[i];
        if (!note_matched[i]) continue;
        if (note.end_tick <= note.tick) continue;  // skip normal notes

        auto head_it = note_head_result.find(i);
        if (head_it == note_head_result.end()) continue;
        Judge head_judge = head_it->second->judge;

        // Find the release event for this LN on the same BMS lane
        for (size_t ri = 0; ri < replay.hits.size(); ++ri) {
            if (release_used[ri]) continue;
            if (replay.hits[ri].is_press) continue;
            int r_bms_lane = display_to_bms[replay.hits[ri].lane];
            if (r_bms_lane != note.lane) continue;

            const auto& rel = replay.hits[ri];
            double sec_rel;
            if (replay.format == ReplayFormat::LR2REP)
                sec_rel = rel.time_sec;
            else
                sec_rel = timeline.time_map.tick_to_second(rel.tick_start);
            double sec_tail = timeline.time_map.tick_to_second(note.end_tick);
            double rel_offset = (sec_rel - sec_tail) * 1000.0;

            Judge tail_judge;
            if (head_judge == Judge::POOR) {
                tail_judge = Judge::BAD;
            } else if (rel_offset < 0 && -rel_offset > win.gd) {
                // Released early by more than GOOD window → BAD
                tail_judge = Judge::BAD;
            } else if (rel_offset > 0 && rel_offset > win.bd_slow) {
                // Released late beyond BAD window → POOR
                tail_judge = Judge::POOR;
            } else {
                tail_judge = head_judge;
            }

            HitResult tail_hr;
            tail_hr.hit       = &rel;
            tail_hr.note      = &note;
            tail_hr.judge     = tail_judge;
            tail_hr.offset_ms = rel_offset;
            tail_hr.is_release = true;
            results_.push_back(tail_hr);
            release_used[ri] = true;
            break;
        }
    }

    // ── Diagnostic: compare local judgments against LR2 op210 gold standard ──
    if (replay.format == ReplayFormat::LR2REP && !replay.lr2_judgements.empty()) {

        struct JudgedNote {
            size_t note_idx;
            Judge  local_judge;
            double offset_ms;
            double exec_time;
            int    bms_lane;
        };
        std::vector<JudgedNote> local_ordered;

        for (auto& r : results_) {
            if (r.is_release) continue;
            if (!r.note) {
                size_t dummy_idx = std::numeric_limits<size_t>::max();
                double exec = r.hit ? r.hit->time_sec : 0.0;
                int lane = r.hit ? static_cast<int>(r.hit->lane) : 0;
                local_ordered.push_back({dummy_idx, Judge::POOR, 0.0, exec, lane});
            } else {
                size_t ni = static_cast<size_t>(r.note - &timeline.notes[0]);
                double exec = r.hit ? r.hit->time_sec : 0.0;
                local_ordered.push_back({ni, r.judge, r.offset_ms, exec,
                                         static_cast<int>(timeline.notes[ni].lane)});
            }
        }

        double bd_slow_sec = static_cast<double>(win.bd_slow) / 1000.0;
        for (const auto* n : missed_notes_) {
            if (n->end_tick != n->tick) continue;
            size_t ni = static_cast<size_t>(n - &timeline.notes[0]);
            double note_sec = timeline.time_map.tick_to_second(n->tick);
            double exec = note_sec + bd_slow_sec;
            local_ordered.push_back({ni, Judge::POOR, 0.0, exec,
                                     static_cast<int>(n->lane)});
        }

        std::sort(local_ordered.begin(), local_ordered.end(),
            [](const JudgedNote& a, const JudgedNote& b) {
                if (a.exec_time != b.exec_time) return a.exec_time < b.exec_time;
                return a.bms_lane < b.bms_lane;
            });

        size_t lr2_count = replay.lr2_judgements.size();
        size_t local_count = local_ordered.size();
        size_t compared = (std::min)(lr2_count, local_count);
        int mismatch_count = 0;
        constexpr int kMaxMismatchLog = 20;

        std::fprintf(stdout, "\n=== LR2 op210 Comparison ===\n");
        std::fprintf(stdout, "lr2_judgements count: %zu\n", lr2_count);
        std::fprintf(stdout, "local judged count:   %zu\n", local_count);

        for (size_t i = 0; i < compared; ++i) {
            uint8_t lr2_val = replay.lr2_judgements[i];
            Judge local_j = local_ordered[i].local_judge;
            uint8_t local_val = judge_to_lr2_val(local_j);

            if (lr2_val != local_val) {
                if (mismatch_count < kMaxMismatchLog) {
                    const auto& jn = local_ordered[i];
                    if (jn.note_idx == std::numeric_limits<size_t>::max()) {
                        std::fprintf(stdout,
                            "Mismatch [EMPTY PRESS lane:%d] "
                            "offset:%.3fms local:%s(%u) replay:%s(%u)\n",
                            jn.bms_lane,
                            jn.offset_ms,
                            lr2_val_name(local_val), local_val,
                            lr2_val_name(lr2_val), lr2_val);
                    } else {
                        const auto& note = timeline.notes[jn.note_idx];
                        std::fprintf(stdout,
                            "Mismatch [idx:%zu lane:%d tick:%lld] "
                            "offset:%.3fms local:%s(%u) replay:%s(%u)\n",
                            jn.note_idx,
                            note.lane,
                            (long long)note.tick,
                            jn.offset_ms,
                            lr2_val_name(local_val), local_val,
                            lr2_val_name(lr2_val), lr2_val);

                        double abs_off = std::fabs(jn.offset_ms);
                        bool boundary = false;
                        if (std::fabs(abs_off - win.pg) < 0.5)       boundary = true;
                        else if (std::fabs(abs_off - win.gr) < 0.5)  boundary = true;
                        else if (std::fabs(abs_off - win.gd) < 0.5)  boundary = true;
                        else if (std::fabs(abs_off - win.bd) < 0.5)  boundary = true;
                        else if (jn.offset_ms < 0 && std::fabs(abs_off - win.bd_fast) < 0.5) boundary = true;
                        else if (jn.offset_ms > 0 && std::fabs(abs_off - win.bd_slow) < 0.5) boundary = true;

                        if (boundary) {
                            std::fprintf(stdout,
                                "  → BOUNDARY: exact=%.6fms  windows PG=%d GR=%d GD=%d BD=%d\n",
                                jn.offset_ms, win.pg, win.gr, win.gd, win.bd);
                        }
                    }
                }
                mismatch_count++;
            }
        }

        if (lr2_count != local_count) {
            std::fprintf(stdout,
                "COUNT MISMATCH: lr2=%zu  local=%zu  (diff=%+zd)\n",
                lr2_count, local_count,
                static_cast<ptrdiff_t>(lr2_count) - static_cast<ptrdiff_t>(local_count));
        }
        if (mismatch_count == 0) {
            std::fprintf(stdout, "ALL JUDGMENTS MATCH replay op210\n");
        } else {
            std::fprintf(stdout, "JUDGMENT MISMATCHES: %d / %zu compared\n",
                         mismatch_count, compared);
        }
        std::fprintf(stdout, "==============================\n\n");
    }

    // → Aggregate stat counts from results_ + missed_notes_
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
    // Missed notes count as POOR (normal notes only, not LN)
    for (auto* n : missed_notes_) {
        if (n->end_tick == n->tick) pr++;
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
    std::fprintf(stdout, "Rank:            %d (%s)\n", rank_raw,
                 JudgeProfile::rank_string(judge_rank));
    std::fprintf(stdout, "Max Window (ms): %d\n", win.bd);
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
        std::fprintf(stdout, "  Mean: %.3f ms  StdDev: %.3f ms\n",
                     stats_.mean_offset, stats_.stddev_offset);
    }
    std::fprintf(stdout, "===================\n\n");
}

const HitResult* JudgementEngine::result_for_hit(size_t hit_index) const {
    if (hit_index < results_.size()) return &results_[hit_index];
    return nullptr;
}

} // namespace bmv
