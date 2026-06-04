#pragma once
#include "replay_data.h"
#include "raw_input_event.h"
#include "core/time_map.h"
#include <string>

namespace bmv {

class Lr2RepParser {
public:
    ReplayData parse(const std::string& filepath, const TimeMap& time_map);

    // New unified interface — returns raw µs events without TimeMap dependency
    ReplayInput parse_replay_input(const std::string& filepath);
};

} // namespace bmv
