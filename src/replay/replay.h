#pragma once
#include "core/types.h"
#include "core/time_map.h"
#include <vector>
#include <cstdint>
#include <string>

namespace bmv {

enum class ReplayFormat {
    BRD,
    LR2REP
};

enum class LR2RandomMode {
    Off     = 0,
    Mirror  = 1,
    Random  = 2,
    SRandom = 3,
    RRandom = 4,
};

struct ReplayHit {
    tick_t  tick_start;
    tick_t  tick_end;
    uint8_t lane;
    uint8_t raw_keycode;
    double  time_sec = 0.0;
    bool    is_press = true;
};

struct ReplayData {
    std::vector<ReplayHit> hits;

    ReplayFormat format = ReplayFormat::BRD;

    bool  has_shuffle = false;
    int   shuffle_pattern[8] = {0, 1, 2, 3, 4, 5, 6, 7};

    bool          has_random_info[2] = {false, false}; // [0]=P1, [1]=P2
    LR2RandomMode random_mode[2]     = {LR2RandomMode::Off, LR2RandomMode::Off};
    int           random_seed     = 0;

    std::vector<uint8_t> lr2_judgements;

    int64_t duration_us = 0;
    int     unmatched   = 0;
};

class IReplayParser {
public:
    virtual ~IReplayParser() = default;
    virtual ReplayData parse(const std::string& filepath,
                             const TimeMap& time_map) = 0;
};

class BrdParser : public IReplayParser {
public:
    ReplayData parse(const std::string& filepath,
                     const TimeMap& time_map) override;
};

class Lr2RepParser;  // declared in lr2rep_parser.h

} // namespace bmv
