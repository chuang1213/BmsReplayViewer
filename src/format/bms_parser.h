#pragma once
#include "raw_data.h"
#include <string>
#include <map>

namespace bmv {

class BmsParser {
public:
    RawChartData parse(const std::string& filepath);

private:
    // Base-36 value decoding (for WAV indices: 0-9, A-Z → 0..35)
    static std::vector<int> decode_channel_values(const std::string& raw);
    // Hex value decoding (for Channel 03 direct BPM: 00-FF)
    static std::vector<int> decode_hex_values(const std::string& raw);
    static int decode_hex_byte(const char* ptr);
    static int decode_base36_byte(const char* ptr);
    static bool is_base36_digit(char c);
    static int base36_digit(char c);

    // key for grouping multi-line channel data (measure, channel)
    using ChanKey = std::pair<int, int>;
    std::map<ChanKey, std::vector<int>> merged_channels_;
};

} // namespace bmv
