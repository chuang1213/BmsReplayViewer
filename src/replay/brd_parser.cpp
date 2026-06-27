#include "brd_parser.h"
#include "replay_adapter.h"
#include "java_random.h"
#include "base64.h"
#include "gzip.h"
#include "json.hpp"

#include <fstream>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <vector>
#include <algorithm>

namespace bmv {

static std::vector<uint8_t> gzip_decompress(const void* data, size_t size) {
    return gzip::decompress(data, size);
}

// --- Sub-functions ---

static std::vector<uint8_t> brd_read_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return {};
    size_t size = static_cast<size_t>(f.tellg());
    f.seekg(0, std::ios::beg);
    std::vector<uint8_t> data(size);
    f.read(reinterpret_cast<char*>(data.data()), size);
    return data;
}

// GZIP-decompress the raw .brd file and parse the inner JSON.
// Returns the decompressed JSON bytes on success, empty on failure.
static std::vector<uint8_t> brd_unwrap_json(const std::vector<uint8_t>& raw,
                                             nlohmann::json& out_json) {
    auto json_bytes = gzip_decompress(raw.data(), raw.size());
    if (json_bytes.empty()) return {};

    std::string json_str(json_bytes.begin(), json_bytes.end());
    auto j = nlohmann::json::parse(json_str, nullptr, false);
    if (j.is_discarded()) {
#ifdef BMV_DEBUG
        std::fprintf(stderr, "[BrdParser] JSON parse failed\n");
#endif
        return {};
    }

    out_json = std::move(j);
    return json_bytes;
}

// Detect BRD format version by checking JSON field names.
// Returns: "new" (keyinput), "old" (keylog), "unknown"
static const char* brd_detect_version(const nlohmann::json& j) {
    if (j.contains("keyinput") && j["keyinput"].is_string()) {
        return "new";
    }
    if (j.contains("keylog") && j["keylog"].is_array()) {
        return "old";
    }
    return "unknown";
}

// Parse 9-byte key frames from binary stream.
// Each frame: byte[0]=signed keycode (positive=press, negative=release),
// byte[1..8]=int64_t timestamp LE.
// Physical keycode → unified lane mapping: keycode 7→lane 0(scratch), 0..6→lane 1..7(keys).
// Produces SEPARATE press/release RawInputEvent entries (no pairing).
static std::vector<RawInputEvent> brd_decode_frames(const std::vector<uint8_t>& bin,
                                                      const nlohmann::json& /*j*/,
                                                      int64_t& out_duration_us) {
    std::vector<RawInputEvent> events;

    if (bin.size() % 9 != 0) {
#ifdef BMV_DEBUG
        std::fprintf(stderr, "[BrdParser] warning: binary stream size %zu not multiple of 9\n",
                     bin.size());
#endif
    }

    size_t frames = bin.size() / 9;
    for (size_t i = 0; i < frames; ++i) {
        const uint8_t* p = &bin[i * 9];

        int8_t val = static_cast<int8_t>(p[0]);
        bool pressed = (val > 0);
        int keycode = std::abs(static_cast<int>(val)) - 1;

        if (keycode < 0 || keycode > 7) continue;

        int64_t ts = 0;
        std::memcpy(&ts, p + 1, 8);

        // Physical keycode → unified display lane
        uint8_t lane;
        if (keycode == 7)      lane = 0;         // Scratch → lane 0
        else /* keycode 0..6*/ lane = static_cast<uint8_t>(keycode + 1); // K1-K7 → lane 1-7

        events.push_back({ts, lane, pressed});
    }

    if (!events.empty()) out_duration_us = events.back().time_us;
    return events;
}

// Parse keylog array from legacy BRD format.
// Each element: {"presstime": int64, "keycode": int 0-7, "pressed": bool (optional)}
// keycode: 0-6 = keys (lane 1-7), 7 = scratch (lane 0)
static std::vector<RawInputEvent> brd_decode_keylog(const nlohmann::json& keylog,
                                                      int64_t& out_duration_us) {
    std::vector<RawInputEvent> events;

    for (const auto& entry : keylog) {
        if (!entry.is_object()) continue;

        if (!entry.contains("presstime") || !entry["presstime"].is_number_integer()) {
            continue;
        }
        int64_t ts = entry["presstime"].get<int64_t>();

        if (!entry.contains("keycode") || !entry["keycode"].is_number_integer()) {
            continue;
        }
        int keycode = entry["keycode"].get<int>();
        if (keycode < 0 || keycode > 7) continue;

        bool pressed = false;
        if (entry.contains("pressed") && entry["pressed"].is_boolean()) {
            pressed = entry["pressed"].get<bool>();
        }

        // keycode -> lane mapping (same as new format)
        uint8_t lane;
        if (keycode == 7)      lane = 0;         // Scratch -> lane 0
        else /* keycode 0-6 */ lane = static_cast<uint8_t>(keycode + 1); // K0-K6 -> lane 1-7

        events.push_back({ts, lane, pressed});
    }

    if (!events.empty()) out_duration_us = events.back().time_us;
    return events;
}

// Extract BRD-specific metadata (shuffle pattern) from JSON.
static BrdMeta brd_extract_meta(const nlohmann::json& j) {
    BrdMeta meta;

    if (j.contains("laneShufflePattern") && j["laneShufflePattern"].is_array()) {
        auto& outer = j["laneShufflePattern"];
        if (!outer.empty() && outer[0].is_array() && outer[0].size() == 8) {
            // 新 BRD 的 laneShufflePattern 是 beatoraja 格式，需要转换为统一格式
            int beatoraja_pattern[8];
            for (int i = 0; i < 8; ++i) {
                if (outer[0][i].is_number_integer()) {
                    beatoraja_pattern[i] = outer[0][i].get<int>();
                } else {
                    beatoraja_pattern[i] = i;
                }
            }
            
            // 坐标转换：beatoraja → 统一格式
            for (int display_bev = 0; display_bev < 8; ++display_bev) {
                int bms_bev = beatoraja_pattern[display_bev];
                int display_our = (display_bev == 7) ? 0 : display_bev + 1;
                int bms_our = (bms_bev == 7) ? 0 : bms_bev + 1;
                meta.shuffle_pattern[display_our] = bms_our;
            }
            meta.has_shuffle = true;
        }
    } else if (j.contains("randomoption") && j.contains("randomoptionseed")) {
        // 旧 BRD 没有 laneShufflePattern，但有 seed，需要计算
        int random_option = j["randomoption"].get<int>();
        int64_t seed = j["randomoptionseed"].get<int64_t>();
        
        // 只有 random_option == 2 或 9 时才需要计算 shuffle
        if (random_option == 2 || random_option == 9) {
            build_brd_random_pattern(random_option, seed, meta.shuffle_pattern);
            meta.has_shuffle = true;
        }
    }

    return meta;
}

// --- Unified parse (no TimeMap dependency) ---

ReplayInput BrdParser::parse_replay_input(const std::string& filepath) {
    ReplayInput input;
    input.format = ReplayFormat::BRD;

    auto raw = brd_read_file(filepath);
    if (raw.empty()) {
        return input;
    }

    nlohmann::json j;
    auto json_bytes = brd_unwrap_json(raw, j);
    if (json_bytes.empty()) return input;

    input.brd = brd_extract_meta(j);

    const char* version = brd_detect_version(j);

    if (std::strcmp(version, "new") == 0) {
        // New format: keyinput (base64 + GZIP)
        std::string keyinput = j["keyinput"].get<std::string>();

        auto decoded = base64::decode(keyinput);
        if (decoded.empty()) {
#ifdef BMV_DEBUG
            std::fprintf(stderr, "[BrdParser] base64 decode produced no data\n");
#endif
            return input;
        }

        auto bin = gzip_decompress(decoded.data(), decoded.size());
        if (bin.empty()) return input;

        input.events = brd_decode_frames(bin, j, input.duration_us);

    } else if (std::strcmp(version, "old") == 0) {
        // Legacy format: keylog (JSON array)
        const auto& keylog = j["keylog"];
        input.events = brd_decode_keylog(keylog, input.duration_us);

    } else {
#ifdef BMV_DEBUG
        std::fprintf(stderr, "[BrdParser] unknown BRD format: missing keyinput or keylog\n");
#endif
        return input;
    }

    return input;
}

// --- Legacy parse (delegates to unified + adapter) ---

ReplayData BrdParser::parse(const std::string& filepath,
                             const TimeMap& time_map) {
    ReplayInput input = parse_replay_input(filepath);
    if (input.events.empty()) return {};

    ReplayData result = replay_input_to_replay_data(input, time_map);

    return result;
}

} // namespace bmv
