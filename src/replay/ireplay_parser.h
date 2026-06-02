#pragma once
#include "replay_data.h"
#include "core/time_map.h"
#include <string>

namespace bmv {

class IReplayParser {
public:
    virtual ~IReplayParser() = default;
    virtual ReplayData parse(const std::string& filepath,
                             const TimeMap& time_map) = 0;
};

} // namespace bmv
