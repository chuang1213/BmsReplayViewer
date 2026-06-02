#pragma once
#include "types.h"
#include "time_map.h"
#include <vector>
#include <string>

namespace bmv {

struct RawChartData;

struct Timeline {
    std::vector<NoteEvent>    notes;
    std::vector<BpmEvent>     bpm_changes;
    std::vector<StopEvent>    stops;
    std::vector<MeasureLine>  measures;
    std::vector<BgmEvent>     bgm;

    TimeMap time_map;

    std::string title;
    std::string artist;
    std::string genre;
    double      initial_bpm = 130.0;
    int         play_level  = 1;
    int         rank         = 2;

    tick_t tick_begin() const;
    tick_t tick_end() const;
    double tick_to_second(tick_t tick) const;
    tick_t second_to_tick(double seconds) const;
};

Timeline build_timeline(const RawChartData& raw);

} // namespace bmv
