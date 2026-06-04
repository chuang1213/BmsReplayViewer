#include "brd_parser.h"
#include "replay_adapter.h"
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
        std::fprintf(stderr, "[BrdParser] JSON parse failed\n");
        return {};
    }

    // TODO: Phase X - BRD version compatibility
    // [位置保留] 旧版本 BRD 格式检测与分支逻辑
    // 根据 JSON 结构/版本号判断，调用不同的解析流程
    // 当前: 仅支持最新版本
    // Version detection stub:
    // int brd_version = detect_brd_version(j);
    // if (brd_version < 0) {
    //     std::fprintf(stderr, "[BrdParser] Unknown BRD version, attempting latest format...\n");
    // }

    out_json = std::move(j);
    return json_bytes;
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
        std::fprintf(stderr, "[BrdParser] warning: binary stream size %zu not multiple of 9\n",
                     bin.size());
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

// Extract BRD-specific metadata (shuffle pattern) from JSON.
static BrdMeta brd_extract_meta(const nlohmann::json& j) {
    BrdMeta meta;

    if (j.contains("laneShufflePattern") && j["laneShufflePattern"].is_array()) {
        auto& outer = j["laneShufflePattern"];
        if (!outer.empty() && outer[0].is_array() && outer[0].size() == 8) {
            for (int i = 0; i < 8; ++i) {
                if (outer[0][i].is_number_integer()) {
                    meta.shuffle_pattern[i] = outer[0][i].get<int>();
                    meta.has_shuffle = true;
                }
            }
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
        std::fprintf(stderr, "[BrdParser] cannot read file: %s\n", filepath.c_str());
        return input;
    }

    nlohmann::json j;
    auto json_bytes = brd_unwrap_json(raw, j);
    if (json_bytes.empty()) return input;

    input.brd = brd_extract_meta(j);

    if (!j.contains("keyinput") || !j["keyinput"].is_string()) {
        std::fprintf(stderr, "[BrdParser] missing 'keyinput' field\n");
        return input;
    }
    std::string keyinput = j["keyinput"].get<std::string>();

    auto decoded = base64::decode(keyinput);
    if (decoded.empty()) {
        std::fprintf(stderr, "[BrdParser] base64 decode produced no data\n");
        return input;
    }

    auto bin = gzip_decompress(decoded.data(), decoded.size());
    if (bin.empty()) return input;

    input.events = brd_decode_frames(bin, j, input.duration_us);
    return input;
}

// --- Legacy parse (delegates to unified + adapter) ---

ReplayData BrdParser::parse(const std::string& filepath,
                             const TimeMap& time_map) {
    ReplayInput input = parse_replay_input(filepath);
    if (input.events.empty()) return {};

    ReplayData result = replay_input_to_replay_data(input, time_map);

    std::fprintf(stdout, "[BrdParser] %s: %zu hits, %d unmatched, shuffle=%s\n",
                 filepath.c_str(), result.hits.size(), result.unmatched,
                 result.has_shuffle ? "YES" : "NO");

    return result;
}

} // namespace bmv
