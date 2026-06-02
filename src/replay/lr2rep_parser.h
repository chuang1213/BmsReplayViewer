#pragma once
#include "replay.h"

namespace bmv {

class Lr2RepParser : public IReplayParser {
public:
    ReplayData parse(const std::string& filepath,
                     const TimeMap& time_map) override;
};

} // namespace bmv
