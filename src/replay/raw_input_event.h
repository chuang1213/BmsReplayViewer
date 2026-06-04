#pragma once
#include "replay_data.h"
#include <vector>
#include <cstdint>

namespace bmv {

struct RawInputEvent {
    int64_t time_us; // Timestamp in microseconds since replay start
    uint8_t lane;    // Unified display lane: 0=scratch, 1-7=keys (physical numbering, pre-shuffle)
    bool    down;    // true=press (key down), false=release (key up)
};

struct Lr2Meta {
    // Per-player random mode info (index 0=P1, 1=P2)
    bool          has_random_info[2] = {false, false};
    LR2RandomMode random_mode[2]     = {LR2RandomMode::Off, LR2RandomMode::Off};
    int           seed               = 0;       // MT19937 seed for LR2 random mode
    std::vector<uint8_t> op210;                 // Recorded judgement values from op210 events
};

struct BrdMeta {
    bool has_shuffle          = false;          // Whether laneShufflePattern exists in BRD
    int  shuffle_pattern[8]   = {0, 1, 2, 3, 4, 5, 6, 7}; // Display lane → BMS lane mapping
};

struct ReplayInput {
    ReplayFormat             format;            // Source format: BRD or LR2REP
    std::vector<RawInputEvent> events;          // Time-ordered raw press/release events
    int64_t                  duration_us = 0;   // Last event timestamp (microseconds)
    Lr2Meta                  lr2;               // LR2-specific metadata (populated for LR2REP)
    BrdMeta                  brd;               // BRD-specific metadata (populated for BRD)
};

} // namespace bmv
