#include "lr2rep_parser.h"
#include <fstream>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <vector>

namespace bmv {

template<typename T>
static T read_le(const uint8_t* p) {
    T v = 0;
    for (size_t i = 0; i < sizeof(T); ++i)
        v |= static_cast<T>(p[i]) << (i * 8);
    return v;
}

static uint8_t lr2_op_to_lane(int op) {
    if (op == 0 || op == 10) return 0;
    if (op >= 1 && op <= 7)  return static_cast<uint8_t>(op);
    return 0xFF;
}

ReplayData Lr2RepParser::parse(const std::string& filepath,
                                const TimeMap& time_map) {
    ReplayData result;
    result.format = ReplayFormat::LR2REP;

    std::ifstream f(filepath, std::ios::binary | std::ios::ate);
    if (!f) {
        std::fprintf(stderr, "[Lr2RepParser] cannot open: %s\n", filepath.c_str());
        return result;
    }
    size_t size = static_cast<size_t>(f.tellg());
    f.seekg(0, std::ios::beg);
    std::vector<uint8_t> buf(size);
    f.read(reinterpret_cast<char*>(buf.data()), size);

    if (size % 12 != 0) {
        std::fprintf(stderr, "[Lr2RepParser] invalid file size %zu (not multiple of 12)\n",
                     size);
        return result;
    }

    int raw_random_mode_p1 = 0;
    int raw_random_mode_p2 = 0;
    int recorded_hits   = 0;

    for (size_t i = 0; i < size; i += 12) {
        int32_t time_ms = read_le<int32_t>(&buf[i + 0]);
        int32_t op      = read_le<int32_t>(&buf[i + 4]);
        int32_t value   = read_le<int32_t>(&buf[i + 8]);

        if (op < 40) {
            uint8_t lane = lr2_op_to_lane(op);
            if (lane > 7) continue;

            double sec = static_cast<double>(time_ms) / 1000.0;
            tick_t tick = time_map.second_to_tick(sec);

            ReplayHit hit;
            hit.tick_start  = tick;
            hit.tick_end    = tick;
            hit.lane        = lane;
            hit.raw_keycode = static_cast<uint8_t>(op);
            hit.time_sec    = sec;
            hit.is_press    = (value == 1);
            result.hits.push_back(hit);

            if (value == 1) recorded_hits++;
        } else {
            if (op == 103) {
                raw_random_mode_p1 = value;
                if (value >= 0 && value <= 4) {
                    result.has_random_info[0] = true;
                    result.random_mode[0] = static_cast<LR2RandomMode>(value);
                } else {
                    std::fprintf(stderr, "[Lr2RepParser] P1 invalid random_mode=%d, defaulting OFF\n", value);
                }
            } else if (op == 153) {
                raw_random_mode_p2 = value;
                if (value >= 0 && value <= 4) {
                    result.has_random_info[1] = true;
                    result.random_mode[1] = static_cast<LR2RandomMode>(value);
                } else {
                    std::fprintf(stderr, "[Lr2RepParser] P2 invalid random_mode=%d, defaulting OFF\n", value);
                }
            } else if (op == 200) {
                result.random_seed = value;
            } else if (op == 210) {
                result.lr2_judgements.push_back(static_cast<uint8_t>(value));
            }
        }
    }

    auto mode_name = [](LR2RandomMode m) -> const char* {
        if (m == LR2RandomMode::Mirror)  return "MIRROR";
        if (m == LR2RandomMode::Random)  return "RANDOM";
        if (m == LR2RandomMode::SRandom) return "S-RANDOM";
        if (m == LR2RandomMode::RRandom) return "R-RANDOM";
        return "OFF";
    };

    std::fprintf(stdout,
        "[Lr2RepParser] %s: %zu raw hits (%d KeyDown), format=LR2REP\n"
        "  P1 Random Mode: %s  P2 Random Mode: %s  |  Seed: %d  |  op210: %zu\n",
        filepath.c_str(), result.hits.size(), recorded_hits,
        mode_name(result.random_mode[0]), mode_name(result.random_mode[1]),
        result.random_seed, result.lr2_judgements.size());

    return result;
}

} // namespace bmv
