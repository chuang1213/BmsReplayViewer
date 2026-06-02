#pragma once
#include "types.h"
#include <vector>

namespace bmv {

struct TimeMapEntry {
    tick_t tick;                  // tick at which this segment starts
    double bpm;                   // effective bpm (> 0) in this segment
    double accumulated_seconds;   // total seconds accumulated at this tick
    double stop_seconds = 0.0;    // non-zero if this entry ends a STOP
};

class TimeMap {
public:
    void build(const std::vector<BpmEvent>& bpms,
               const std::vector<StopEvent>& stops,
               double initial_bpm);

    double tick_to_second(tick_t tick) const;
    tick_t second_to_tick(double seconds) const;

    bool empty() const { return entries_.empty(); }

private:
    std::vector<TimeMapEntry> entries_;
};

} // namespace bmv
