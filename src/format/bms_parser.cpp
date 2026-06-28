#include "bms_parser.h"
#include "util/encoding.h"
#include "util/fs_util.h"
#include <sstream>
#include <cstdio>
#include <cstdlib>
#include <cctype>

namespace bmv {

// --- Hex helpers (Channel 03 direct BPM: 00-FF) ---

int BmsParser::decode_hex_byte(const char* ptr) {
    auto h = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        return 0;
    };
    return h(ptr[0]) * 16 + h(ptr[1]);
}

std::vector<int> BmsParser::decode_hex_values(const std::string& raw) {
    std::vector<int> result;
    size_t len = raw.size();
    if (len % 2 != 0) return result;
    result.reserve(len / 2);
    for (size_t i = 0; i < len; i += 2) {
        result.push_back(decode_hex_byte(&raw[i]));
    }
    return result;
}

// --- Base-36 helpers ---
// BME channel values and #WAV indices are base-36: 0-9, A-Z (case-insensitive)

bool BmsParser::is_base36_digit(char c) {
    return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

int BmsParser::base36_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'Z') return c - 'A' + 10;
    if (c >= 'a' && c <= 'z') return c - 'a' + 10;
    return 0;
}

int BmsParser::decode_base36_byte(const char* ptr) {
    if (!is_base36_digit(ptr[0]) || !is_base36_digit(ptr[1])) return -1;
    int hi = base36_digit(ptr[0]);
    int lo = base36_digit(ptr[1]);
    return hi * 36 + lo;
}

std::vector<int> BmsParser::decode_channel_values(const std::string& raw) {
    std::vector<int> result;
    size_t len = raw.size();
    if (len % 2 != 0) return result;
    result.reserve(len / 2);
    for (size_t i = 0; i < len; i += 2) {
        int val = decode_base36_byte(&raw[i]);
        if (val < 0) {
            std::fprintf(stderr, "[warn] decode_channel_values: invalid base36 pair at offset %zu\n", i);
            continue;
        }
        result.push_back(val);
    }
    return result;
}

// --- Main parse ---

RawChartData BmsParser::parse(const std::string& filepath)
{
    RawChartData chart;
    merged_channels_.clear();

    // 以二进制模式读取并自动检测编码，统一转换为 UTF-8
    // （日文 BMS 文件通常为 Shift-JIS / CP932 编码）
    std::string content = read_file_as_utf8(filepath);
    if (content.empty()) {
        // 区分"文件不存在"与"空文件"：用 file_size 探测
        if (file_size(filepath) < 0) {
            std::fprintf(stderr, "Error: cannot open file '%s'\n", filepath.c_str());
        }
        return chart;
    }

    std::istringstream ss(content);
    std::string line;
    int line_num = 0;

    while (std::getline(ss, line)) {
        line_num++;

        if (!line.empty() && line.back() == '\r') line.pop_back();

        if (line.empty()) continue;
        if (line.size() < 2) continue;
        if (line[0] == '*') continue;
        if (line[0] != '#') continue;

        // --- Header / definition lines: #KEY value ---
        if (line[1] >= 'A' && line[1] <= 'Z') {
            size_t space = line.find(' ');
            std::string key, value;
            if (space != std::string::npos) {
                key   = line.substr(1, space - 1);
                value = line.substr(space + 1);
            } else {
                key   = line.substr(1);
                value = "";
            }

            if (key == "TITLE") {
                chart.title = value;
            } else if (key == "ARTIST") {
                chart.artist = value;
            } else if (key == "GENRE") {
                chart.genre = value;
            } else if (key == "BPM") {
                double bpm = std::strtod(value.c_str(), nullptr);
                if (bpm > 0.0) chart.initial_bpm = bpm;
            } else if (key == "PLAYLEVEL") {
                chart.play_level = static_cast<int>(std::strtol(value.c_str(), nullptr, 10));
            } else if (key.substr(0, 3) == "WAV") {
                // #WAVxx ← base-36 index
                std::string idx_str = key.substr(3);
                int idx = static_cast<int>(std::strtol(idx_str.c_str(), nullptr, 36));
                chart.wav_defs.push_back({idx, value});
            } else if (key.substr(0, 3) == "BMP") {
                // ignored
            } else if (key.substr(0, 3) == "BPM" && key.size() > 3) {
                // #BPMxx → extended BPM definition (base-36 index)
                std::string idx_str = key.substr(3);
                int idx = static_cast<int>(std::strtol(idx_str.c_str(), nullptr, 36));
                double val = std::strtod(value.c_str(), nullptr);
                if (val > 0.0) chart.bpm_defs[idx] = val;
            } else if (key.substr(0, 4) == "STOP") {
                // #STOPxx → store as beats (value / 192.0)
                std::string idx_str = key.substr(4);
                int idx = static_cast<int>(std::strtol(idx_str.c_str(), nullptr, 36));
                double raw = std::strtod(value.c_str(), nullptr);
                chart.stop_defs[idx] = raw / 192.0;
            } else if (key == "LNOBJ") {
                // #LNOBJ XX YY ... (space-separated base-36 WAV indices)
                const char* p = value.c_str();
                while (*p) {
                    while (*p == ' ') ++p;
                    if (!*p) break;
                    // read two base-36 chars
                    int v = 0;
                    if (is_base36_digit(p[0])) {
                        v = base36_digit(p[0]) * 36;
                        if (is_base36_digit(p[1])) v += base36_digit(p[1]);
                        else v = base36_digit(p[0]);
                        chart.lnobj_set.insert(v);
                    }
                    // skip this token
                    while (*p && *p != ' ') ++p;
                }
            } else if (key == "PLAYER" || key == "TOTAL" ||
                       key == "STAGEFILE" || key == "DIFFICULTY" ||
                       key == "SUBTITLE" || key == "SUBARTIST" ||
                       key == "COMMENT" || key == "BANNER" || key == "BACKBMP" ||
                       key == "LNTYPE") {
                // recognized but ignored
            } else if (key == "RANK") {
                int r = static_cast<int>(std::strtol(value.c_str(), nullptr, 10));
                if (r >= 0 && r <= 4) chart.rank = r;  // 4 = VERY EASY (beatoraja)
            }
            continue;
        }

        // --- Channel data: #MMMCC:VV... ---
        if (line[1] < '0' || line[1] > '9') {
            std::fprintf(stderr, "[warn] malformed line %d: %s\n", line_num, line.c_str());
            continue;
        }

        if (line.size() < 7) {
            std::fprintf(stderr, "[warn] line too short %d: %s\n", line_num, line.c_str());
            continue;
        }

        std::string mmm = line.substr(1, 3);
        std::string cc  = line.substr(4, 2);

        int measure = static_cast<int>(std::strtol(mmm.c_str(), nullptr, 10));
        int channel = static_cast<int>(std::strtol(cc.c_str(), nullptr, 16));

        size_t colon = line.find(':');
        if (colon == std::string::npos || colon + 1 >= line.size()) continue;

        std::string val_str = line.substr(colon + 1);

        // Channel 02: measure length fraction (decimal, not base-36)
        if (channel == 0x02) {
            double fraction = std::strtod(val_str.c_str(), nullptr);
            if (fraction > 0.0) {
                chart.measure_lengths[measure] = fraction;
            }
            continue;
        }

        // Channel 03: direct BPM — must use HEX (not base-36)
        //   "55" → 0x55 = 85, "AA" → 0xAA = 170
        if (channel == 0x03) {
            std::vector<int> values = decode_hex_values(val_str);
            if (values.empty()) continue;
            ChanKey key = {measure, channel};
            auto& merged = merged_channels_[key];
            merged.insert(merged.end(), values.begin(), values.end());
            continue;
        }

        std::vector<int> values = decode_channel_values(val_str);
        if (values.empty()) continue;

        ChanKey key = {measure, channel};
        auto& merged = merged_channels_[key];
        merged.insert(merged.end(), values.begin(), values.end());
    }

    for (auto& [key, values] : merged_channels_) {
        RawChartData::ChannelData cd;
        cd.measure = key.first;
        cd.channel = key.second;
        cd.values  = std::move(values);
        chart.channels.push_back(std::move(cd));
    }

    std::fprintf(stdout, "[info] parsed %s: %zu channels (merged), %zu WAV defs, "
                 "BPM=%.1f\n",
                 filepath.c_str(), chart.channels.size(), chart.wav_defs.size(),
                 chart.initial_bpm);

    return chart;
}

} // namespace bmv
