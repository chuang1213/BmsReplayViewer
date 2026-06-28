#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace bmv {

struct ReplayEvent {
    int64_t time_us;
    uint8_t lane;
};

enum class RandomMode {
    Off = 0,
    Mirror = 1,
    Random = 2,
    SRandom = 3,
    RRandom = 4,
};

struct ReplayMetadata {
    std::array<int, 8> shuffle_pattern = {0, 1, 2, 3, 4, 5, 6, 7};
    std::optional<RandomMode> random_option;
    std::optional<std::vector<uint8_t>> judgements;
    std::optional<int> seed;
    int64_t duration_us = 0;
};

struct UnifiedReplay {
    std::vector<ReplayEvent> press_events;
    std::vector<ReplayEvent> release_events;
    ReplayMetadata metadata;
};

struct ParseError {
    std::string file_path;
    std::string stage;
    std::string reason;
    std::string context;
};

}
