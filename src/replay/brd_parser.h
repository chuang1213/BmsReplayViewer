#pragma once
#include "ireplay_parser.h"
#include <string>

namespace bmv {

class BrdParser : public IReplayParser {
public:
    ReplayData parse(const std::string& filepath,
                     const TimeMap& time_map) override;
};

} // namespace bmv
