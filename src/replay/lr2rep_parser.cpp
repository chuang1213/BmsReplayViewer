#include "lr2rep_parser.h"
#include "replay_adapter.h"
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
    // op 0 = scratch (P1 buttonInput[0], the judged turntable lane).
    // op 10 = the OTHER turntable direction → ignored (not judged in LR2).
    if (op == 0)             return 0;
    if (op >= 1 && op <= 7)  return static_cast<uint8_t>(op);
    return 0xFF;
}

// --- Sub-functions ---

static std::vector<uint8_t> lr2_read_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return {};
    size_t size = static_cast<size_t>(f.tellg());
    f.seekg(0, std::ios::beg);
    std::vector<uint8_t> buf(size);
    f.read(reinterpret_cast<char*>(buf.data()), size);
    return buf;
}

// Parse 12-byte records → RawInputEvent list (unpaired press/release).
// Each record: time_ms(i32) | op(i32) | value(i32).
// op < 40: key event → {time_us, lane, down} via lr2_op_to_lane()
// op >= 40: metadata → Lr2Meta (op 103/153/200/210)
// Input is time-ascending (checked by LR2 spec).
static std::vector<RawInputEvent> lr2_decode_records(const std::vector<uint8_t>& buf,
                                                       Lr2Meta& out_meta,
                                                       int64_t& out_duration_us) {
    std::vector<RawInputEvent> events;
    size_t size = buf.size();

    if (size % 12 != 0) {
        std::fprintf(stderr, "[Lr2RepParser] invalid file size %zu (not multiple of 12)\n", size);
        return events;
    }

    for (size_t i = 0; i < size; i += 12) {
        int32_t time_ms = read_le<int32_t>(&buf[i + 0]);
        int32_t op      = read_le<int32_t>(&buf[i + 4]);
        int32_t value   = read_le<int32_t>(&buf[i + 8]);

        if (op < 40) {
            uint8_t lane = lr2_op_to_lane(op);
            if (lane > 7) continue;

            int64_t time_us = static_cast<int64_t>(time_ms) * 1000;
            bool down = (value == 1);

            events.push_back({time_us, lane, down});
        } else {
            if (op == 103) {
                if (value >= 0 && value <= 4) {
                    out_meta.has_random_info[0] = true;
                    out_meta.random_mode[0] = static_cast<LR2RandomMode>(value);
                } else {
                    std::fprintf(stderr, "[Lr2RepParser] P1 invalid random_mode=%d, defaulting OFF\n", value);
                }
            } else if (op == 153) {
                if (value >= 0 && value <= 4) {
                    out_meta.has_random_info[1] = true;
                    out_meta.random_mode[1] = static_cast<LR2RandomMode>(value);
                } else {
                    std::fprintf(stderr, "[Lr2RepParser] P2 invalid random_mode=%d, defaulting OFF\n", value);
                }
            } else if (op == 200) {
                out_meta.seed = value;
            } else if (op == 210) {
                out_meta.op210.push_back(static_cast<uint8_t>(value));
            }
        }
    }

    if (!events.empty()) {
        out_duration_us = events.back().time_us;
    }

    return events;
}

// --- Unified parse (no TimeMap dependency) ---

ReplayInput Lr2RepParser::parse_replay_input(const std::string& filepath) {
    ReplayInput input;
    input.format = ReplayFormat::LR2REP;

    auto buf = lr2_read_file(filepath);
    if (buf.empty()) {
        std::fprintf(stderr, "[Lr2RepParser] cannot open: %s\n", filepath.c_str());
        return input;
    }

    input.events = lr2_decode_records(buf, input.lr2, input.duration_us);

    auto mode_name = [](LR2RandomMode m) -> const char* {
        if (m == LR2RandomMode::Mirror)  return "MIRROR";
        if (m == LR2RandomMode::Random)  return "RANDOM";
        if (m == LR2RandomMode::SRandom) return "S-RANDOM";
        if (m == LR2RandomMode::RRandom) return "R-RANDOM";
        return "OFF";
    };

    std::fprintf(stdout,
        "[Lr2RepParser] %s: %zu raw events, format=LR2REP\n"
        "  P1 Random Mode: %s  P2 Random Mode: %s  |  Seed: %d  |  op210: %zu\n",
        filepath.c_str(), input.events.size(),
        mode_name(input.lr2.random_mode[0]), mode_name(input.lr2.random_mode[1]),
        input.lr2.seed, input.lr2.op210.size());

    return input;
}

// --- Legacy parse (delegates to unified + adapter) ---

ReplayData Lr2RepParser::parse(const std::string& filepath,
                                const TimeMap& time_map) {
    ReplayInput input = parse_replay_input(filepath);
    if (input.events.empty()) return {};

    ReplayData result = replay_input_to_replay_data(input, time_map);

    auto mode_name = [](LR2RandomMode m) -> const char* {
        if (m == LR2RandomMode::Mirror)  return "MIRROR";
        if (m == LR2RandomMode::Random)  return "RANDOM";
        if (m == LR2RandomMode::SRandom) return "S-RANDOM";
        if (m == LR2RandomMode::RRandom) return "R-RANDOM";
        return "OFF";
    };

    int recorded_hits = 0;
    for (auto& h : result.hits) if (h.is_press) recorded_hits++;

    std::fprintf(stdout,
        "[Lr2RepParser] %s: %zu hits (%d KeyDown), format=LR2REP\n"
        "  P1 Random Mode: %s  P2 Random Mode: %s  |  Seed: %d  |  op210: %zu\n",
        filepath.c_str(), result.hits.size(), recorded_hits,
        mode_name(result.random_mode[0]), mode_name(result.random_mode[1]),
        result.random_seed, result.lr2_judgements.size());

    return result;
}

} // namespace bmv
