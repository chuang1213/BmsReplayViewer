#include "replay_adapter.h"
#include <cstdio>

namespace bmv {

// Convert unified lane (0=scratch, 1-7=keys) back to raw keycode (scratch=7, K1-K7=0-6)
static uint8_t lane_to_raw_keycode(uint8_t lane) {
    if (lane == 0) return 7;
    return lane - 1;
}

ReplayData replay_input_to_replay_data(const ReplayInput& input,
                                        const TimeMap& time_map) {
    ReplayData result;
    result.format     = input.format;
    result.duration_us = input.duration_us;

    if (input.format == ReplayFormat::BRD) {
        // --- BRD: state-machine pairing ---

        // TODO: Phase X - Support legacy BRD versions
        // [位置保留] 如果 input 是旧版本 BRD，可能需要不同的配对策略
        // 当前: 假设事件中 press/release 语义一致
        // 未来: 根据 brd_version（从 input.brd.version 传入，如有）调用不同配对函数

        result.has_shuffle = input.brd.has_shuffle;
        for (int i = 0; i < 8; ++i)
            result.shuffle_pattern[i] = input.brd.shuffle_pattern[i];

        struct KeyState {
            bool    active     = false;
            tick_t  start_tick = 0;
            int     lane       = -1;
            int64_t start_us   = 0;
        };
        KeyState keys[8]; // indexed by unified lane (0=scratch, 1-7=keys)
        tick_t last_tick = 0;

        for (auto& ev : input.events) {
            double seconds = static_cast<double>(ev.time_us) / 1'000'000.0;
            tick_t tick = time_map.second_to_tick(seconds);
            last_tick = tick;

            int lane = ev.lane;

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

        // EOF cutoff: any still-active keys
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

    } else if (input.format == ReplayFormat::LR2REP) {
        // --- LR2: direct event-to-hit conversion ---

        result.has_random_info[0] = input.lr2.has_random_info[0];
        result.has_random_info[1] = input.lr2.has_random_info[1];
        result.random_mode[0]     = input.lr2.random_mode[0];
        result.random_mode[1]     = input.lr2.random_mode[1];
        result.random_seed        = input.lr2.seed;
        result.lr2_judgements     = input.lr2.op210;

        for (auto& ev : input.events) {
            double sec = static_cast<double>(ev.time_us) / 1'000'000.0;
            tick_t tick = time_map.second_to_tick(sec);

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
