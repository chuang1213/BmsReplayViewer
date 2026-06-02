#include "time_map.h"
#include <algorithm>

namespace bmv {

void TimeMap::build(const std::vector<BpmEvent>& bpms,
                    const std::vector<StopEvent>& stops,
                    double initial_bpm)
{
    entries_.clear();

    struct TimePoint {
        tick_t tick;
        bool   is_stop = false;
        double stop_beats = 0.0;
        double bpm = 0.0;
    };
    std::vector<TimePoint> points;
    for (auto& b : bpms) {
        points.push_back({b.tick, false, 0.0, b.bpm > 0.0 ? b.bpm : -b.bpm});
    }
    for (auto& s : stops) {
        points.push_back({s.tick, true, s.stop_beats, 0.0});
    }

    // sort by tick; at same tick: BPM before STOP
    std::sort(points.begin(), points.end(), [](const TimePoint& a, const TimePoint& b) {
        if (a.tick != b.tick) return a.tick < b.tick;
        return !a.is_stop && b.is_stop;
    });

    double current_bpm     = initial_bpm;
    tick_t current_tick    = 0;
    double accumulated_sec = 0.0;

    entries_.push_back({0, current_bpm, 0.0});

    for (auto& p : points) {
        if (p.tick == 0 && !p.is_stop) {
            current_bpm = p.bpm;
            entries_[0].bpm = current_bpm;
            continue;
        }
        if (p.tick == 0 && p.is_stop) {
            continue;
        }

        tick_t delta_tick = p.tick - current_tick;
        double delta_beat = static_cast<double>(delta_tick) / TPB;
        double delta_sec  = delta_beat * 60.0 / current_bpm;

        if (p.is_stop) {
            double stop_sec = p.stop_beats * 60.0 / current_bpm;
            accumulated_sec += delta_sec + stop_sec;
            // STOP does not advance tick
            current_tick = p.tick;
        } else {
            accumulated_sec += delta_sec;
            current_tick = p.tick;
            current_bpm  = p.bpm;
        }

        entries_.push_back({current_tick, current_bpm, accumulated_sec});
    }
}

double TimeMap::tick_to_second(tick_t tick) const
{
    if (entries_.empty()) return 0.0;
    if (tick <= 0) return 0.0;

    auto it = std::upper_bound(entries_.begin(), entries_.end(), tick,
        [](tick_t t, const TimeMapEntry& e) { return t < e.tick; });

    if (it == entries_.begin()) return 0.0;
    --it;

    const auto& entry = *it;
    tick_t delta_tick = tick - entry.tick;
    double delta_beat = static_cast<double>(delta_tick) / TPB;
    double delta_sec  = delta_beat * 60.0 / entry.bpm;

    return entry.accumulated_seconds + delta_sec;
}

tick_t TimeMap::second_to_tick(double seconds) const
{
    if (entries_.empty()) return 0;
    if (seconds <= 0.0) return 0;

    auto it = std::upper_bound(entries_.begin(), entries_.end(), seconds,
        [](double s, const TimeMapEntry& e) { return s < e.accumulated_seconds; });

    if (it == entries_.begin()) return 0;
    --it;

    const auto& entry = *it;
    double remaining_sec = seconds - entry.accumulated_seconds;
    double remaining_beat = remaining_sec * entry.bpm / 60.0;
    tick_t remaining_ticks = static_cast<tick_t>(remaining_beat * TPB);

    return entry.tick + remaining_ticks;
}

} // namespace bmv
