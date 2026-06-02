#include "timeline.h"
#include "format/raw_data.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <cstdio>

namespace bmv {

tick_t Timeline::tick_begin() const
{
    tick_t t = std::numeric_limits<tick_t>::max();
    if (!notes.empty())    t = std::min(t, notes.front().tick);
    if (!bpm_changes.empty()) t = std::min(t, bpm_changes.front().tick);
    if (!stops.empty())    t = std::min(t, stops.front().tick);
    if (!measures.empty()) t = std::min(t, measures.front().tick);
    if (!bgm.empty())      t = std::min(t, bgm.front().tick);
    return (t == std::numeric_limits<tick_t>::max()) ? 0 : t;
}

tick_t Timeline::tick_end() const
{
    tick_t t = 0;
    if (!notes.empty())    t = std::max(t, notes.back().tick);
    if (!bpm_changes.empty()) t = std::max(t, bpm_changes.back().tick);
    if (!stops.empty())    t = std::max(t, stops.back().tick);
    if (!measures.empty()) t = std::max(t, measures.back().tick);
    if (!bgm.empty())      t = std::max(t, bgm.back().tick);
    return t;
}

double Timeline::tick_to_second(tick_t tick) const
{
    return time_map.tick_to_second(tick);
}

tick_t Timeline::second_to_tick(double seconds) const
{
    return time_map.second_to_tick(seconds);
}

// Build a cumulative measure map from Channel 02 definitions.
// Default measure length = TICKS_PER_MEASURE unless overridden.
static std::vector<MeasureInfo> build_measure_map(const RawChartData& raw)
{
    std::vector<MeasureInfo> map;

    // Determine max measure from channels and measure_lengths
    int max_measure = 0;
    for (auto& ch : raw.channels) {
        if (ch.measure > max_measure) max_measure = ch.measure;
    }
    for (auto& [m, _] : raw.measure_lengths) {
        if (m > max_measure) max_measure = m;
    }

    tick_t current = 0;
    for (int m = 0; m <= max_measure + 1; ++m) {
        tick_t length = TICKS_PER_MEASURE;
        bool    explicit_len = false;
        auto it = raw.measure_lengths.find(m);
        if (it != raw.measure_lengths.end() && it->second > 0.0) {
            length = static_cast<tick_t>(TICKS_PER_MEASURE * it->second);
            explicit_len = true;
        }
        MeasureInfo info;
        info.measure         = m;
        info.start_tick      = current;
        info.length_tick     = length;
        info.explicit_length = explicit_len;
        map.push_back(info);
        current += length;
    }
    return map;
}

Timeline build_timeline(const RawChartData& raw)
{
    Timeline tl;

    tl.title       = raw.title;
    tl.artist      = raw.artist;
    tl.genre       = raw.genre;
    tl.initial_bpm = raw.initial_bpm;
    tl.play_level  = raw.play_level;
    tl.rank        = raw.rank;

    auto measure_map = build_measure_map(raw);

    // Per-channel pending head (LNOBJ semantics)
    struct LNState {
        bool   active    = false;
        tick_t head_tick = 0;
        int    head_wav  = 0;
        int    head_lane = 0;
        int    head_meas = 0;
    };
    std::map<int, LNState> ln_states;

    // Helper: lane index from BMS channel
    static auto channel_lane = [](int ch) -> int {
        switch (ch) {
            case 0x16: return 0;  // Scratch
            case 0x11: return 1;  // K1
            case 0x12: return 2;  // K2
            case 0x13: return 3;  // K3
            case 0x14: return 4;  // K4
            case 0x15: return 5;  // K5
            case 0x18: return 6;  // K6
            case 0x19: return 7;  // K7
            // 0x17 (Scratch right) intentionally skipped for 7K; falls through to default
            default: return -1;
        }
    };

    // Helper: emit a NoteEvent from an LN state and clear it
    int total_yy_seen    = 0;
    int total_yy_matched = 0;
    int total_yy_orphan  = 0;
    int total_ln_emitted = 0;

    struct OrphanLog { int meas; int channel; tick_t tick; };
    std::vector<OrphanLog> orphan_log;

    auto emit_normal = [&](int channel) {
        auto& st = ln_states[channel];
        if (!st.active) return;
        NoteEvent ev;
        ev.tick      = st.head_tick;
        ev.end_tick  = st.head_tick;          // normal note: end == start
        ev.lane      = static_cast<uint8_t>(st.head_lane);
        ev.wav_index = static_cast<uint16_t>(st.head_wav);
        tl.notes.push_back(ev);
        st.active = false;
    };

    auto emit_ln = [&](int channel, tick_t end_tick_val) {
        auto& st = ln_states[channel];
        if (!st.active) return;
        NoteEvent ev;
        ev.tick      = st.head_tick;
        ev.end_tick  = end_tick_val;          // LN: end from pairing LNOBJ
        ev.lane      = static_cast<uint8_t>(st.head_lane);
        ev.wav_index = static_cast<uint16_t>(st.head_wav);
        tl.notes.push_back(ev);
        total_ln_emitted++;
        total_yy_matched++;
        st.active = false;
    };

    // process channel data into events
    for (auto& ch : raw.channels) {
        tick_t measure_start  = 0;
        tick_t measure_length = TICKS_PER_MEASURE;
        if (ch.measure >= 0 && static_cast<size_t>(ch.measure) < measure_map.size()) {
            measure_start  = measure_map[ch.measure].start_tick;
            measure_length = measure_map[ch.measure].length_tick;
        }

        int num_steps = static_cast<int>(ch.values.size());
        if (num_steps == 0) continue;

        if (ch.channel == 0x01) {
            // BGM
            for (int i = 0; i < num_steps; ++i) {
                int val = ch.values[i];
                if (val == 0) continue;
                tick_t step_tick = measure_start + (measure_length * i) / num_steps;
                BgmEvent ev;
                ev.tick      = step_tick;
                ev.wav_index = static_cast<uint16_t>(val);
                tl.bgm.push_back(ev);
            }
        } else if (ch.channel >= 0x11 && ch.channel <= 0x19) {
            int lane = channel_lane(ch.channel);
            if (lane < 0) continue;

            auto& st = ln_states[ch.channel];

            for (int i = 0; i < num_steps; ++i) {
                int val = ch.values[i];
                tick_t step_tick = measure_start + (measure_length * i) / num_steps;

                bool is_lnobj = (raw.lnobj_set.count(val) > 0);

                if (is_lnobj) {
                    // LNOBJ marker (YY)
                    total_yy_seen++;
                    if (st.active) {
                        // Pair with pending head → emit LN
                        emit_ln(ch.channel, step_tick);
                    } else {
                        total_yy_orphan++;
                        orphan_log.push_back({ch.measure, ch.channel, step_tick});
                    }
                } else if (val != 0) {
                    // Normal note: replace pending head
                    // (previous head, if any, is a normal note that was never paired)
                    emit_normal(ch.channel);
                    st.active    = true;
                    st.head_tick = step_tick;
                    st.head_wav  = val;
                    st.head_lane = lane;
                    st.head_meas = ch.measure;
                }
                // val == 0: do nothing
            }
        } else if (ch.channel == 0x03) {
            for (int i = 0; i < num_steps; ++i) {
                int val = ch.values[i];
                if (val == 0) continue;
                tick_t step_tick = measure_start + (measure_length * i) / num_steps;
                BpmEvent ev;
                ev.tick   = step_tick;
                ev.bpm    = static_cast<double>(val);
                ev.source = BpmSource::CH03;
                if (ev.bpm > 0.0) tl.bpm_changes.push_back(ev);
            }
        } else if (ch.channel == 0x08) {
            for (int i = 0; i < num_steps; ++i) {
                int val = ch.values[i];
                if (val == 0) continue;
                tick_t step_tick = measure_start + (measure_length * i) / num_steps;
                auto it = raw.bpm_defs.find(val);
                if (it != raw.bpm_defs.end()) {
                    BpmEvent ev;
                    ev.tick   = step_tick;
                    ev.bpm    = it->second;
                    ev.source = BpmSource::CH08;
                    tl.bpm_changes.push_back(ev);
                }
            }
        } else if (ch.channel == 0x09) {
            for (int i = 0; i < num_steps; ++i) {
                int val = ch.values[i];
                if (val == 0) continue;
                tick_t step_tick = measure_start + (measure_length * i) / num_steps;
                auto it = raw.stop_defs.find(val);
                if (it != raw.stop_defs.end()) {
                    StopEvent ev;
                    ev.tick       = step_tick;
                    ev.stop_beats = it->second;
                    tl.stops.push_back(ev);
                }
            }
        }
    }

    // Finalize remaining pending heads at chart end (all are normal notes)
    int pending_at_eof = 0;
    if (!measure_map.empty()) {
        for (auto& [ch, st] : ln_states) {
            if (st.active) {
                pending_at_eof++;
                emit_normal(ch);
            }
        }
    }

    // sort events
    std::sort(tl.notes.begin(), tl.notes.end(),
        [](const NoteEvent& a, const NoteEvent& b) {
            if (a.tick != b.tick) return a.tick < b.tick;
            return a.lane < b.lane;
        });

    std::sort(tl.bgm.begin(), tl.bgm.end(),
        [](const BgmEvent& a, const BgmEvent& b) { return a.tick < b.tick; });

    std::sort(tl.bpm_changes.begin(), tl.bpm_changes.end(),
        [](const BpmEvent& a, const BpmEvent& b) { return a.tick < b.tick; });

    std::sort(tl.stops.begin(), tl.stops.end(),
        [](const StopEvent& a, const StopEvent& b) { return a.tick < b.tick; });

#ifdef BMV_DEBUG
    // ================ BPM SOURCE AUDIT ================
    int bpm_invalid = 0;
    std::fprintf(stdout, "\n=== BPM Source Audit ===\n");
    std::fprintf(stdout, "value     | tick      | source  | valid\n");
    std::fprintf(stdout, "----------------------------------------\n");
    for (auto& b : tl.bpm_changes) {
        const char* src_str = "???";
        switch (b.source) {
            case BpmSource::HEADER: src_str = "HEADER"; break;
            case BpmSource::CH03:   src_str = "Ch03";   break;
            case BpmSource::CH08:   src_str = "Ch08";   break;
        }
        bool valid = (b.source == BpmSource::CH03 || b.source == BpmSource::CH08);
        if (!valid) bpm_invalid++;
        std::fprintf(stdout, "%-9.1f | %9lld | %-7s | %s\n",
                     b.bpm, (long long)b.tick, src_str, valid ? "YES" : "NO");
    }
    if (tl.bpm_changes.empty()) {
        std::fprintf(stdout, "  (none — using #BPM header only: %.1f)\n", tl.initial_bpm);
    }

    // ================ MEASURE AUDIT ================
    std::fprintf(stdout, "\n=== Measure Length Audit ===\n");
    std::fprintf(stdout, "measure | length   | source\n");
    std::fprintf(stdout, "----------------------------\n");
    for (auto& mi : measure_map) {
        double frac = static_cast<double>(mi.length_tick) / TICKS_PER_MEASURE;
        const char* src = mi.explicit_length ? "Ch02" : "default";
        std::fprintf(stdout, " %3d    | %7.3f | %s\n", mi.measure, frac, src);
    }

    // ================ LN INTEGRITY ================
    std::fprintf(stdout, "\n=== LN Integrity ===\n");
    std::fprintf(stdout, "LN count: %d\n", total_ln_emitted);
    std::fprintf(stdout, "orphan:   %d\n", total_yy_orphan);
    if (total_yy_orphan == 0) {
        std::fprintf(stdout, "Status:   OK\n");
    } else {
        std::fprintf(stdout, "Status:   WARNING — %d orphan LNOBJ markers\n", total_yy_orphan);
    }
#endif

    // build measure lines from the map
    for (auto& mi : measure_map) {
        MeasureLine ml;
        ml.tick        = mi.start_tick;
        ml.measure_num = mi.measure;
        ml.num         = 4;
        ml.den         = 4;
        tl.measures.push_back(ml);
    }

    tl.time_map.build(tl.bpm_changes, tl.stops, tl.initial_bpm);

    return tl;
}

} // namespace bmv
