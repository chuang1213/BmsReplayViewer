#pragma once
#include <cstdint>

namespace bmv {

using tick_t = int64_t;

constexpr tick_t TPB              = 1920;
constexpr tick_t TICKS_PER_MEASURE = TPB * 4;  // 7680, assuming 4/4

struct NoteEvent {
    tick_t   tick;
    tick_t   end_tick;   // > tick = long note; == tick = normal note
    uint8_t  lane;
    uint16_t wav_index;
    // Non-truncated note position (fractional ticks). `tick` is this floored by
    // integer division; beatoraja judges on exact µs, so the µs path uses this to
    // avoid sub-tick (~0.16 ms @190 BPM) error flipping FAST/SLOW near 0 ms.
    double   tick_exact = 0.0;
};

enum class BpmSource : uint8_t {
    HEADER,  // #BPM initial value
    CH03,    // direct BPM in channel 03
    CH08     // extended BPM via #BPMxx definition
};

struct BpmEvent {
    tick_t    tick;
    double    bpm;
    BpmSource source;
};

struct StopEvent {
    tick_t tick;
    double stop_beats;  // stop duration in beats (e.g. 0.125 = 24/192)
};

struct MeasureLine {
    tick_t tick;
    int    measure_num;
    uint8_t num;        // time signature numerator (default 4)
    uint8_t den;        // time signature denominator (default 4)
};

struct BgmEvent {
    tick_t   tick;
    uint16_t wav_index;  // #WAV index (base-36: 1..ZZ=1295)
};

struct MeasureInfo {
    int     measure;
    tick_t  start_tick;
    tick_t  length_tick;
    bool    explicit_length;  // true if defined via Channel 02
};

} // namespace bmv
