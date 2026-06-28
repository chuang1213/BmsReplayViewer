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
    // Unified lane coordinate system: 0=scratch, 1-7=keys (physical/display lanes).
    // ReplayHit.lane is already in display-lane space after brd_decode_frames / lr2_decode_records.
    // This function maps display_lane → bms_lane (for note comparison) and bms_lane → display_lane (for rendering).
    for (int i = 0; i < 8; ++i) {
        computed_display_to_bms_[i] = i;
        computed_bms_to_display_[i] = i;
    }

    if (replay.format == ReplayFormat::BRD && replay.has_shuffle) {
        // shuffle_pattern 已经是统一格式：shuffle_pattern[display_lane] = bms_lane
        // 其中 0 = scratch, 1-7 = keys
        for (int i = 0; i < 8; ++i) {
            int bms_lane = replay.shuffle_pattern[i];
            computed_display_to_bms_[i] = bms_lane;
            computed_bms_to_display_[bms_lane] = i;
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
    if (rank_raw < 0) rank_raw = 0;
    const int rank_max = (system_ == JudgeSystem::Beatoraja) ? 4 : 3;  // beatoraja adds VERY EASY
    if (rank_raw > rank_max) rank_raw = rank_max;
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

    // → beatoraja SEVENKEYS judging — faithful to vanilla beatoraja JudgeManager.java.
    //   dmtime = note_us − press_us (>0 = early/FAST). Each press picks the CLOSEST unmatched
    //   note in [BD-late, MS-early] (beatoraja's JudgeAlgorithm), then the first tier [lo,hi]
    //   containing dmtime decides the grade:
    //     • PG/GR/GD/BD → score the note, consume it (combo continues)
    //     • MS tier     → 空POOR: counts as POOR, does NOT consume the note and does NOT break
    //                      combo. The note can still be scored by a later press, so one note may
    //                      appear as BOTH a POOR and a hit — which is why the judge total can
    //                      exceed the note count (verified: anata = 2252 notes, 2260 judges,
    //                      Δ = 8 空POOR). Scratch uses its own wider windows.
    //   A note no press ever scores → POOR (miss). #RANK 4 (VERY EASY) supported.
    //   (LN/mine + exact JudgeAlgorithm tie-break still simplified.)
    if (system_ == JudgeSystem::Beatoraja) {
        // Faithful port of beatoraja JudgeManager.update() for SEVENKEYS notes.
        //   • Each note has a state: 0 = unjudged (hittable). A HIT (PG/GR/GD/BD) or an
        //     auto-MISS consumes it (judgeVanish {T,T,T,T,T,F} → setState). 空POOR (MS,
        //     judge 5) does NOT consume (judgeVanish[5]=false): the note stays hittable,
        //     and an already-consumed note can still take a 空POOR (the state≠0 branch).
        //   • dmtime = note_us − press_us (>0 ⇒ pressed early/FAST). fast/slow = (dmtime≥0)
        //     exactly as score.addJudgeCount(judge, mfast>=0) (JudgeManager:673).
        //   • Auto-miss is TIME-driven (JudgeManager:612-618): once the play clock passes
        //     note+|BD_late| with the note unhit it becomes judge 4 POOR, recorded LATE
        //     (mfast = note − mtime < 0). |BD_late| = −W[3].lo (280 ms note / 290 ms scr).
        //   • Per-press selection = JudgeManager:399-434 with JudgeAlgorithm.Combo gate.
        for (auto& kv : lane_notes) {
            int bms_lane = kv.first;
            const auto& notes = kv.second;
            const auto& hits  = lane_hits.count(bms_lane) ? lane_hits[bms_lane]
                                                          : std::vector<size_t>{};
            BeatorajaWindow W[5];
            JudgeProfile::beatoraja_windows(rank_raw, /*scratch=*/bms_lane == 0, W);
            // mjudgestart/mjudgeend: the selection considers dmtime ∈ [find_lo, find_hi).
            long long find_lo = W[0].lo_us, find_hi = W[0].hi_us;
            for (int j = 0; j < 5; ++j) {
                if (W[j].lo_us < find_lo) find_lo = W[j].lo_us;  // BD late (most negative)
                if (W[j].hi_us > find_hi) find_hi = W[j].hi_us;  // MS early (most positive)
            }
            const long long gd_lo = W[2].lo_us, gd_hi = W[2].hi_us;  // GOOD window (Combo gate)
            const long long ms_lo = W[4].lo_us, ms_hi = W[4].hi_us;  // MS window (空POOR / re-POOR)
            const long long miss_after = -W[3].lo_us;  // auto-miss this long after the note

            std::vector<long long> nus(notes.size());
            for (size_t i = 0; i < notes.size(); ++i)
                // beatoraja stores note time as (long)microsec — TRUNCATION toward zero,
                // not round-to-nearest (Section.java:528-530). Matching this is what aligns
                // FAST/SLOW: an llround would push X.5+ up a µs and flip the sign vs the game.
                nus[i] = static_cast<long long>(
                    timeline.time_map.tick_to_second(timeline.notes[notes[i]].tick_exact) * 1e6);
            std::vector<char> consumed(notes.size(), 0);  // state≠0 (hit or auto-missed)

            size_t cur = 0;  // first note still reachable by a press (dmtime ≥ find_lo)
            for (size_t hi = 0; hi < hits.size(); ++hi) {
                const auto& hit = replay.hits[hits[hi]];
                if (!hit.is_press) continue;
                // Exact recorded press µs (both .brd and .lr2rep store the press time in
                // time_sec) — avoids the second_to_tick→tick_to_second round-trip that
                // quantized the press to the ~0.16 ms tick grid.
                long long p_us = static_cast<long long>(std::llround(hit.time_sec * 1e6));

                // (1) Time-driven auto-miss: any unconsumed note the clock has already passed
                //     by more than |BD_late| can never be hit → judge 4 POOR, recorded LATE.
                while (cur < notes.size() && nus[cur] + miss_after < p_us) {
                    if (!consumed[cur]) {
                        missed_notes_.push_back(&timeline.notes[notes[cur]]);
                        missed_notes_count++;
                        consumed[cur] = 1;
                    }
                    cur++;
                }

                // (2) Select the note beatoraja judges (JudgeManager:399-434, Combo).
                //     Iterate earliest-first; classify by tier (fresh) or re-POOR (consumed).
                int best = -1, best_judge = 0;
                for (size_t i = cur; i < notes.size(); ++i) {
                    long long dm = nus[i] - p_us;
                    if (dm >= find_hi) break;            // dmtime ≥ mjudgeend
                    if (dm < find_lo) continue;          // dmtime < mjudgestart
                    // Combo gate (JudgeManager:411 + JudgeAlgorithm.Combo): consider note i
                    // when nothing picked yet, the pick is consumed, or the pick is beyond
                    // GOOD-late while a fresh i sits within GOOD-early (preserve combo).
                    bool gate = (best < 0) || consumed[best]
                              || (nus[best] < p_us + gd_lo && !consumed[i] && nus[i] <= p_us + gd_hi);
                    if (!gate) continue;
                    int judge_i;
                    if (consumed[i]) {
                        // already-judged note → re-POOR if within MS, else not selectable
                        judge_i = (dm >= ms_lo && dm <= ms_hi) ? 5 : 6;
                    } else {
                        int ji = 0;
                        for (; ji < 5 && !(dm >= W[ji].lo_us && dm <= W[ji].hi_us); ++ji) {}
                        judge_i = (ji >= 4) ? ji + 1 : ji;   // 0-3 hit, 4(MS)→5 空POOR, 5→6 none
                    }
                    if (judge_i < 6) {
                        long long abs_b = best < 0 ? 0 : (nus[best] - p_us < 0 ? p_us - nus[best] : nus[best] - p_us);
                        long long abs_i = dm < 0 ? -dm : dm;
                        if (judge_i < 4 || best < 0 || abs_b > abs_i) { best = static_cast<int>(i); best_judge = judge_i; }
                    } else {
                        best = -1;  // tnote = null
                    }
                }
                if (best < 0) continue;               // no note in range → nothing recorded

                long long dmtime = nus[best] - p_us;  // >0 early/FAST, <0 late/SLOW
                HitResult hr;
                hr.hit       = &hit;
                hr.offset_ms = -static_cast<double>(dmtime) / 1000.0;  // press − note
                if (dmtime >= 0) hr.fast = true; else hr.slow = true;  // (mfast>=0)=early

                if (best_judge <= 3) {
                    // PG/GR/GD/BD — score and consume (judgeVanish true).
                    hr.note  = &timeline.notes[notes[best]];
                    hr.judge = (best_judge == 0) ? Judge::PGREAT : (best_judge == 1) ? Judge::GREAT
                             : (best_judge == 2) ? Judge::GOOD   : Judge::BAD;
                    results_.push_back(hr);
                    offsets_for_stats.push_back(hr.offset_ms);
                    note_matched[notes[best]] = true;
                    consumed[best] = 1;
                    matched_hits++;
                } else {
                    // judge 5 → 空POOR: POOR, note NOT consumed (judgeVanish[5]=false).
                    hr.judge = Judge::POOR;
                    results_.push_back(hr);
                    unmatched_hits++;
                }
            }

            // (3) End of chart: every still-unconsumed note auto-misses (LATE POOR).
            for (size_t i = cur; i < notes.size(); ++i)
                if (!consumed[i]) {
                    missed_notes_.push_back(&timeline.notes[notes[i]]);
                    missed_notes_count++;
                }
        }
    }

    // → Process each BMS lane independently — faithful port of LR2 ProcSinglenote
    //   (OpenLR2 LR2/Scene04_Play.cpp). Per lane a note cursor advances; each key
    //   press judges AT MOST one note. LR2 rules reproduced:
    //     • press − note > BD            → that note MISSED   (op210 value 1)
    //     • |press − note| ≤ BD          → PG/GR/GD/BD cascade, note consumed
    //     • note ahead, BD < ahead <POOR → empty POOR         (op210 value 0), note kept
    //     • note ahead ≥ POOR (1000 ms)  → press ignored, nothing recorded
    //   (BAD→next-note carry at Scene04_Play.cpp:852 omitted for now — rare.)
    if (system_ != JudgeSystem::Beatoraja)
    for (auto& kv : lane_notes) {
        int bms_lane = kv.first;
        const auto& notes = kv.second;  // sorted by tick
        const auto& hits  = lane_hits.count(bms_lane) ? lane_hits[bms_lane]
                                                        : std::vector<size_t>{};
        size_t si = 0;  // note cursor index within this lane's notes

        const int    bd       = std::max(win.bd_fast, win.bd_slow); // BAD window (ms)
        const double bd_sec   = static_cast<double>(bd) / 1000.0;
        const double poor_sec = static_cast<double>(win.poor) / 1000.0;

        // LR2 judges against INTEGER-millisecond note times (note.realTiming is whole
        // ms); we otherwise keep sub-ms precision, which flips ~15 boundary notes
        // (e.g. our 21.68 ms → PG, but LR2's truncated 22 ms → GR). Truncate note time
        // to int ms for LR2 to match exactly. Beatoraja keeps full (µs) precision.
        const bool lr2_int_ms = (replay.format == ReplayFormat::LR2REP);
        auto note_ms = [&](const NoteEvent& n) -> double {
            double ms = timeline.time_map.tick_to_second(n.tick) * 1000.0;
            // +1e-6 guards against a whole-ms note time computed as N-epsilon by
            // float error (which would truncate to N-1 and flip a boundary note).
            return lr2_int_ms ? std::trunc(ms + 1e-6) : ms;
        };

        for (size_t hi = 0; hi < hits.size(); ++hi) {
            const auto& hit = replay.hits[hits[hi]];
            if (!hit.is_press) continue;  // releases handled in LN tail pass

            double sec_hit = (replay.format == ReplayFormat::LR2REP)
                           ? hit.time_sec
                           : timeline.time_map.tick_to_second(hit.tick_start);
            // LR2 replay timestamps are whole ms; recover the exact integer (sec_hit =
            // time_ms/1000, and *1000 alone leaves float roundtrip error that can flip a
            // boundary note, e.g. -22 ms showing as -21.9996 → truncates to 21 → PGREAT).
            double press_ms = lr2_int_ms ? std::round(sec_hit * 1000.0) : sec_hit * 1000.0;

            // 1. Miss every note this press is already more than BD past (POOR, value 1).
            while (si < notes.size()) {
                const auto& note = timeline.notes[notes[si]];
                double sec_note = timeline.time_map.tick_to_second(note.tick);
                if (sec_hit - sec_note <= bd_sec) break;   // current note still reachable / ahead
                missed_notes_.push_back(&note);
                missed_notes_count++;
                si++;
            }

            // No note remains on this lane → LR2 records nothing for this press.
            if (si >= notes.size()) continue;

            size_t note_idx  = notes[si];
            const auto& note = timeline.notes[note_idx];
            double sec_note  = timeline.time_map.tick_to_second(note.tick);
            double offset_ms = press_ms - note_ms(note);
            int    abs_ms    = static_cast<int>(std::fabs(offset_ms)); // LR2 truncates the gap: abs(ftol)

            // 2. Within BAD window → judge this note (ascending cascade) and consume it.
            //    LR2 (Scene04_Play.cpp:852): after a BAD, the SAME press also judges the
            //    next note if it too is within BD — chaining through consecutive BADs.
            if (abs_ms <= bd) {
                bool carry = true;
                while (carry && si < notes.size()) {
                    size_t ni       = notes[si];
                    const auto& n   = timeline.notes[ni];
                    double off      = press_ms - note_ms(n);
                    int    a        = static_cast<int>(std::fabs(off)); // LR2 truncates the gap: abs(ftol)
                    if (a > bd) break;   // this note not within the BAD window of the press

                    HitResult hr;
                    hr.hit       = &hit;
                    hr.note      = &n;
                    hr.offset_ms = off;
                    if (off < 0) hr.fast = true; else if (off > 0) hr.slow = true;

                    if      (a <= win.pg) hr.judge = Judge::PGREAT;
                    else if (a <= win.gr) hr.judge = Judge::GREAT;
                    else if (a <= win.gd) hr.judge = Judge::GOOD;
                    else                  hr.judge = Judge::BAD;

                    results_.push_back(hr);
                    offsets_for_stats.push_back(off);
                    note_matched[ni] = true;
                    matched_hits++;
                    si++;
                    carry = (hr.judge == Judge::BAD);   // only a BAD carries to the next note
                }
                continue;
            }

            // 3. gap > BD. Step 1 already missed any note the press is past, so the
            //    current note is AHEAD. Empty POOR (value 0) only if it is within the
            //    POOR window ahead; if further than POOR (1000 ms), LR2 ignores the
            //    press entirely. Either way the note is NOT consumed.
            double note_ahead_sec = sec_note - sec_hit;
            if (note_ahead_sec < poor_sec) {
                HitResult hr;
                hr.hit       = &hit;
                hr.judge     = Judge::POOR;   // empty POOR — maps to op210 value 0
                hr.offset_ms = offset_ms;
                hr.fast      = true;
                results_.push_back(hr);
                unmatched_hits++;
            }
            // else: note ≥ POOR ms ahead → press ignored (no record).
        }

        // Notes after the last press on this lane → missed (POOR, value 1).
        while (si < notes.size()) {
            const auto& note = timeline.notes[notes[si]];
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
    // Map note index → INDEX into results_ (not a pointer): the loop below calls
    // results_.push_back(), which can reallocate the vector and would invalidate
    // any stored HitResult* (use-after-free). Indices remain valid across
    // reallocation.
    std::map<size_t, size_t> note_head_result;
    for (size_t ri = 0; ri < results_.size(); ++ri) {
        const auto& r = results_[ri];
        if (!r.is_release && r.note) {
            size_t note_idx = static_cast<size_t>(r.note - &timeline.notes[0]);
            note_head_result[note_idx] = ri;
        }
    }

    std::vector<bool> release_used(replay.hits.size(), false);

    for (size_t i = 0; i < timeline.notes.size(); ++i) {
        const auto& note = timeline.notes[i];
        if (!note_matched[i]) continue;
        if (note.end_tick <= note.tick) continue;  // skip normal notes

        auto head_it = note_head_result.find(i);
        if (head_it == note_head_result.end()) continue;
        Judge head_judge = results_[head_it->second].judge;

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
            // Empty POOR (a stray press with no note) → op210 value 0, not 1.
            uint8_t local_val =
                (local_ordered[i].note_idx == std::numeric_limits<size_t>::max())
                    ? 0
                    : judge_to_lr2_val(local_j);

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
            std::fprintf(stdout, "JUDGMENT MISMATCHES: %d / %zu compared (position-aligned; desyncs on empty-POOR count)\n",
                         mismatch_count, compared);
        }

        // ── Per-note fidelity (order-robust): compare each note's judgement to the
        //    NON-empty op210 entries (values 1-5 = exactly one per note), dropping the
        //    value-0 empty-POORs on both sides so their count no longer desyncs it.
        {
            std::vector<uint8_t> by_note(timeline.notes.size(), 1); // default = missed POOR (1)
            for (auto& r : results_) {
                if (r.is_release || !r.note) continue;
                size_t ni = static_cast<size_t>(r.note - &timeline.notes[0]);
                if (ni < by_note.size()) by_note[ni] = judge_to_lr2_val(r.judge);
            }
            std::vector<uint8_t> lr2_notes;
            lr2_notes.reserve(replay.lr2_judgements.size());
            for (uint8_t v : replay.lr2_judgements) if (v != 0) lr2_notes.push_back(v);

            size_t cmp = (std::min)(by_note.size(), lr2_notes.size());
            int nmis = 0;
            for (size_t i = 0; i < cmp; ++i) if (by_note[i] != lr2_notes[i]) nmis++;
            std::fprintf(stdout,
                "Per-note fidelity: notes=%zu  lr2_note_judgements=%zu  mismatches=%d (%.2f%%)\n",
                by_note.size(), lr2_notes.size(), nmis,
                cmp ? 100.0 * nmis / static_cast<double>(cmp) : 0.0);
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

    // → Per-grade mean offset (diagnostic). A fast-leaning player makes the OVERALL
    //   mean very negative, but the PGREAT-only mean should still sit near 0 if our
    //   note timing matches the game (RE doc measured LR2 PG ≈ -0.6 ms). A large
    //   PG-only mean would instead indicate a systematic note-time error.
    {
        double sumg[4] = {0,0,0,0}; int ng[4] = {0,0,0,0};
        auto gi = [](Judge j){ switch (j) { case Judge::PGREAT: return 0; case Judge::GREAT: return 1;
            case Judge::GOOD: return 2; case Judge::BAD: return 3; default: return -1; } };
        for (auto& r : results_) {
            if (r.is_release || !r.note) continue;
            int k = gi(r.judge); if (k >= 0) { sumg[k] += r.offset_ms; ng[k]++; }
        }
        std::fprintf(stdout, "Per-grade mean offset (ms): PG=%.2f(n=%d) GR=%.2f(n=%d) GD=%.2f(n=%d) BD=%.2f(n=%d)\n",
            ng[0]?sumg[0]/ng[0]:0.0, ng[0], ng[1]?sumg[1]/ng[1]:0.0, ng[1],
            ng[2]?sumg[2]/ng[2]:0.0, ng[2], ng[3]?sumg[3]/ng[3]:0.0, ng[3]);
    }
    // Missed notes count as POOR (normal notes only, not LN). In beatoraja the auto-miss
    // (judge 4) is recorded LATE (mfast = note − mtime < 0), so it also adds to SLOW.
    for (auto* n : missed_notes_) {
        if (n->end_tick == n->tick) {
            pr++;
            if (system_ == JudgeSystem::Beatoraja) slw++;
        }
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
