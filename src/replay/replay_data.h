#pragma once
#include "core/types.h"
#include <vector>
#include <cstdint>

namespace bmv {

struct ReplayHit {
    tick_t  tick_start;   // press tick
    tick_t  tick_end;     // release tick (paired) or EOF cutoff
    uint8_t lane;         // mapped BMV lane (0=SC, 1-7=keys), 0xFF=unmapped
    uint8_t raw_keycode;  // original BRD keycode (0-7), for debug
};

struct ReplayData {
    std::vector<ReplayHit> hits;

    bool  has_shuffle = false;
    int   shuffle_pattern[8] = {0, 1, 2, 3, 4, 5, 6, 7};

    int64_t duration_us = 0;
    int     unmatched   = 0;
};

} // namespace bmv
