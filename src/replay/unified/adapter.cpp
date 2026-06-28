#include "adapter.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

namespace bmv {

namespace {

// unified lane (0=scratch, 1-7=keys) → raw keycode (scratch=7, K1-K7=0-6).
// Mirrors the inverse mapping used in unified/brd_parser.cpp.
uint8_t lane_to_raw_keycode(uint8_t lane) {
    if (lane == 0) return 7;
    return static_cast<uint8_t>(lane - 1);
}

bool pattern_is_identity(const std::array<int, 8>& p) {
    for (int i = 0; i < 8; ++i) {
        if (p[i] != i) return false;
    }
    return true;
}

} // namespace

ReplayData unified_to_replay_data(const UnifiedReplay& replay,
                                   const TimeMap& time_map,
                                   ReplayFormat format) {
    ReplayData result;
    result.format      = format;
    result.duration_us = replay.metadata.duration_us;

    if (format == ReplayFormat::BRD) {
        // --- BRD: merge press + release into time-ordered stream, then pair ---
        for (int i = 0; i < 8; ++i)
            result.shuffle_pattern[i] = replay.metadata.shuffle_pattern[i];
        result.has_shuffle     = !pattern_is_identity(replay.metadata.shuffle_pattern);

        struct MergedEvent {
            int64_t time_us;
            uint8_t lane;
            bool    down;
        };
        std::vector<MergedEvent> merged;
        merged.reserve(replay.press_events.size() + replay.release_events.size());
        for (const auto& e : replay.press_events)
            merged.push_back({e.time_us, e.lane, true});
        for (const auto& e : replay.release_events)
            merged.push_back({e.time_us, e.lane, false});

        // Stable sort by (time_us, down desc) so a press and a release sharing
        // the same timestamp see the press first (matches BRD file ordering
        // where positive (press) frames precede negative (release) frames).
        std::stable_sort(merged.begin(), merged.end(),
                         [](const MergedEvent& a, const MergedEvent& b) {
                             if (a.time_us != b.time_us) return a.time_us < b.time_us;
                             return a.down > b.down; // true before false
                         });

        struct KeyState {
            bool    active     = false;
            tick_t  start_tick = 0;
            int     lane       = -1;
            int64_t start_us   = 0;
        };
        KeyState keys[8]; // indexed by unified lane (0=scratch, 1-7=keys)
        tick_t last_tick = 0;

        for (const auto& ev : merged) {
            const double seconds = static_cast<double>(ev.time_us) / 1'000'000.0;
            const tick_t tick    = time_map.second_to_tick(seconds);
            last_tick = tick;

            const int lane = ev.lane;

            if (ev.down) {
                keys[lane].active     = true;
                keys[lane].start_tick = tick;
                keys[lane].lane       = lane;
                keys[lane].start_us   = ev.time_us;
            } else {
                if (keys[lane].active) {
                    ReplayHit hit;
                    hit.tick_start  = keys[lane].start_tick;
                    hit.tick_end    = tick;
                    hit.lane        = static_cast<uint8_t>(keys[lane].lane);
                    hit.raw_keycode = lane_to_raw_keycode(static_cast<uint8_t>(lane));
                    hit.time_sec    = static_cast<double>(keys[lane].start_us) / 1'000'000.0;
                    hit.is_press    = true;
                    result.hits.push_back(hit);
                    keys[lane].active = false;
                }
            }
        }

        // EOF cutoff: any still-active keys → close at last_tick, count as unmatched.
        for (int k = 0; k < 8; ++k) {
            if (keys[k].active) {
                ReplayHit hit;
                hit.tick_start  = keys[k].start_tick;
                hit.tick_end    = last_tick;
                hit.lane        = static_cast<uint8_t>(keys[k].lane);
                hit.raw_keycode = (k == 0) ? 7 : static_cast<uint8_t>(k - 1);
                hit.time_sec    = static_cast<double>(keys[k].start_us) / 1'000'000.0;
                hit.is_press    = true;
                result.hits.push_back(hit);
                result.unmatched++;
                keys[k].active = false;
            }
        }

    } else if (format == ReplayFormat::LR2REP) {
        // --- LR2: emit each event as its own ReplayHit ---
        if (replay.metadata.random_option.has_value()) {
            result.has_random_info[0] = true;
            result.random_mode[0]     =
                static_cast<LR2RandomMode>(static_cast<int>(*replay.metadata.random_option));
        }
        if (replay.metadata.seed.has_value()) {
            result.random_seed = *replay.metadata.seed;
        }
        if (replay.metadata.judgements.has_value()) {
            result.lr2_judgements = *replay.metadata.judgements;
        }

        // Merge press + release streams by time so the output hit order matches
        // the original record order (time-ascending), as the legacy adapter did.
        struct MergedEvent {
            int64_t time_us;
            uint8_t lane;
            bool    down;
        };
        std::vector<MergedEvent> merged;
        merged.reserve(replay.press_events.size() + replay.release_events.size());
        for (const auto& e : replay.press_events)
            merged.push_back({e.time_us, e.lane, true});
        for (const auto& e : replay.release_events)
            merged.push_back({e.time_us, e.lane, false});
        std::stable_sort(merged.begin(), merged.end(),
                         [](const MergedEvent& a, const MergedEvent& b) {
                             if (a.time_us != b.time_us) return a.time_us < b.time_us;
                             return a.down > b.down;
                         });

        result.hits.reserve(merged.size());
        for (const auto& ev : merged) {
            const double sec  = static_cast<double>(ev.time_us) / 1'000'000.0;
            const tick_t tick = time_map.second_to_tick(sec);

            ReplayHit hit;
            hit.tick_start  = tick;
            hit.tick_end    = tick;
            hit.lane        = ev.lane;
            hit.raw_keycode = ev.lane;
            hit.time_sec    = sec;
            hit.is_press    = ev.down;
            result.hits.push_back(hit);
        }
    }

    return result;
}

} // namespace bmv
