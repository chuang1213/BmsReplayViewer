#pragma once
#include "replay_data.h"
#include <vector>
#include <cstdint>

namespace bmv {

struct RawInputEvent {
    int64_t time_us; // microsecond timestamp
    uint8_t lane;    // 0=scratch, 1-7=keys (physical-unified numbering)
    bool    down;    // true=press, false=release
};

struct Lr2Meta {
    bool          has_random_info[2] = {false, false};
    LR2RandomMode random_mode[2]     = {LR2RandomMode::Off, LR2RandomMode::Off};
    int           seed               = 0;
    std::vector<uint8_t> op210;
};

struct BrdMeta {
    bool has_shuffle          = false;
    int  shuffle_pattern[8]   = {0, 1, 2, 3, 4, 5, 6, 7};
};

struct ReplayInput {
    ReplayFormat             format;
    std::vector<RawInputEvent> events;
    int64_t                  duration_us = 0;
    Lr2Meta                  lr2;
    BrdMeta                  brd;
};

} // namespace bmv
