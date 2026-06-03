#include "replay.h"
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

// --- Read entire binary file ---

static std::vector<uint8_t> read_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return {};
    size_t size = static_cast<size_t>(f.tellg());
    f.seekg(0, std::ios::beg);
    std::vector<uint8_t> data(size);
    f.read(reinterpret_cast<char*>(data.data()), size);
    return data;
}

// --- Main parse ---

ReplayData BrdParser::parse(const std::string& filepath,
                             const TimeMap& time_map)
{
    ReplayData result;

    // Step 1: read raw .brd binary
    auto raw = read_file(filepath);
    if (raw.empty()) {
        std::fprintf(stderr, "[BrdParser] cannot read file: %s\n", filepath.c_str());
        return result;
    }

    // Step 2: GZIP decompress → JSON string
    auto json_bytes = gzip_decompress(raw.data(), raw.size());
    if (json_bytes.empty()) return result;

    std::string json_str(json_bytes.begin(), json_bytes.end());

    // Step 3: parse JSON
    auto j = nlohmann::json::parse(json_str, nullptr, false);
    if (j.is_discarded()) {
        std::fprintf(stderr, "[BrdParser] JSON parse failed\n");
        return result;
    }

    // Step 4: laneShufflePattern (2D array → read [0])
    if (j.contains("laneShufflePattern") && j["laneShufflePattern"].is_array()) {
        auto& outer = j["laneShufflePattern"];
        if (!outer.empty() && outer[0].is_array() && outer[0].size() == 8) {
            for (int i = 0; i < 8; ++i) {
                if (outer[0][i].is_number_integer()) {
                    result.shuffle_pattern[i] = outer[0][i].get<int>();
                    result.has_shuffle = true;
                }
            }
        }
    }

    // Step 5: get keyinput string
    if (!j.contains("keyinput") || !j["keyinput"].is_string()) {
        std::fprintf(stderr, "[BrdParser] missing 'keyinput' field\n");
        return result;
    }
    std::string keyinput = j["keyinput"].get<std::string>();

    // Step 6: URL-safe Base64 decode
    auto decoded = base64::decode(keyinput);
    if (decoded.empty()) {
        std::fprintf(stderr, "[BrdParser] base64 decode produced no data\n");
        return result;
    }

    // Step 7: GZIP decompress → raw binary key stream
    auto bin = gzip_decompress(decoded.data(), decoded.size());
    if (bin.empty()) return result;

    // Step 8: parse 9-byte frames (1 byte key + 8 bytes timestamp LE)
    if (bin.size() % 9 != 0) {
        std::fprintf(stderr, "[BrdParser] warning: binary stream size %zu not multiple of 9\n",
                     bin.size());
    }

    struct RawEvent {
        int     keycode;
        bool    pressed;
        int64_t time_us;
    };
    std::vector<RawEvent> raw_events;

    size_t frames = bin.size() / 9;
    for (size_t i = 0; i < frames; ++i) {
        const uint8_t* p = &bin[i * 9];

        // byte[0] → signed int8_t
        int8_t val = static_cast<int8_t>(p[0]);
        bool pressed = (val > 0);
        int keycode = std::abs(static_cast<int>(val)) - 1;

        // byte[1..8] → int64_t little-endian
        int64_t ts = 0;
        std::memcpy(&ts, p + 1, 8);

        if (keycode >= 0 && keycode <= 7) {
            raw_events.push_back({keycode, pressed, ts});
        }
    }

    if (raw_events.empty()) return result;

    result.duration_us = raw_events.back().time_us;

    // State machine: per-key press → release pairing
    struct KeyState {
        bool    active = false;
        tick_t  start_tick = 0;
        int     mapped_lane = -1;
        int64_t start_us = 0;
    };
    KeyState keys[8];

    tick_t last_tick = 0;

    for (auto& ev : raw_events) {
        double seconds = static_cast<double>(ev.time_us) / 1'000'000.0;
        tick_t tick = time_map.second_to_tick(seconds);
        last_tick = tick;

        // Physical keycode → display lane (no shuffle here; PngRenderer handles chart note shuffle)
        int lane;
        if (ev.keycode == 7) lane = 0;       // Scratch → lane 0
        else if (ev.keycode <= 6) lane = ev.keycode + 1;  // K1-K7 → lane 1-7
        else continue;

        if (ev.pressed) {
            keys[ev.keycode].active     = true;
            keys[ev.keycode].start_tick = tick;
            keys[ev.keycode].mapped_lane = lane;
            keys[ev.keycode].start_us   = ev.time_us;
        } else {
            if (keys[ev.keycode].active) {
                ReplayHit hit;
                hit.tick_start  = keys[ev.keycode].start_tick;
                hit.tick_end    = tick;
                hit.lane        = static_cast<uint8_t>(keys[ev.keycode].mapped_lane);
                hit.raw_keycode = static_cast<uint8_t>(ev.keycode);
                // PRESS time (exact recorded µs). Was mistakenly the release time
                // (ev.time_us here is the key-up event); the press is what gets
                // judged, so store start_us. beatoraja judges on exact µs.
                hit.time_sec    = static_cast<double>(keys[ev.keycode].start_us) / 1'000'000.0;
                hit.is_press    = true;
                result.hits.push_back(hit);
                keys[ev.keycode].active = false;
            }
            // duplicate release: ignore
        }
    }

    // EOF cutoff: any still-active keys
    for (int k = 0; k < 8; ++k) {
        if (keys[k].active) {
            ReplayHit hit;
            hit.tick_start  = keys[k].start_tick;
            hit.tick_end    = last_tick;
            hit.lane        = static_cast<uint8_t>(keys[k].mapped_lane);
            hit.raw_keycode = static_cast<uint8_t>(k);
            hit.time_sec    = static_cast<double>(keys[k].start_us) / 1'000'000.0;
            hit.is_press    = true;
            result.hits.push_back(hit);
            result.unmatched++;
            keys[k].active = false;
        }
    }

    std::fprintf(stdout, "[BrdParser] %s: %zu hits, %d unmatched, shuffle=%s\n",
                 filepath.c_str(), result.hits.size(), result.unmatched,
                 result.has_shuffle ? "YES" : "NO");

    return result;
}

} // namespace bmv
