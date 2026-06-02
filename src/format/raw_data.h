#pragma once
#include <string>
#include <vector>
#include <utility>
#include <map>
#include <set>

namespace bmv {

struct RawChartData {
    struct ChannelData {
        int measure;              // measure number
        int channel;              // BMS channel number (hex, e.g. 0x11)
        std::vector<int> values;  // WAV indices per step (0 = empty)
    };

    std::string title;
    std::string artist;
    std::string genre;
    double      initial_bpm = 130.0;
    int         play_level  = 1;
    int         rank         = 2;  // #RANK: 0=VERY_HARD, 1=HARD, 2=NORMAL, 3=EASY

    std::vector<std::pair<int, std::string>> wav_defs;

    // Channel 02: measure → length fraction
    std::map<int, double> measure_lengths;

    // #BPMxx → bpm value (base-36 key, double value)
    std::map<int, double> bpm_defs;

    // #STOPxx → stop beats (base-36 key, value/192.0)
    std::map<int, double> stop_defs;

    // #LNOBJ → set of WAV indices (base-36) that mark LN tails
    std::set<int> lnobj_set;

    std::vector<ChannelData> channels;
};

} // namespace bmv
