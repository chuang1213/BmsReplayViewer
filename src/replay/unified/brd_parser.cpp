#include "brd_parser.h"
#include "random.h"

#include "../base64.h"
#include "../gzip.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace bmv {

namespace {

// Set error fields in a single call.
void set_error(ParseError* error, std::string stage, std::string reason, std::string context) {
    if (!error) return;
    error->stage   = std::move(stage);
    error->reason  = std::move(reason);
    error->context = std::move(context);
}

// Map physical keycode (0-7) → unified display lane (0=scratch, 1-7=keys).
// keycode 7 = scratch → lane 0
// keycode 0-6 = keys  → lane 1-7
constexpr uint8_t keycode_to_lane(int keycode) {
    return (keycode == 7) ? 0 : static_cast<uint8_t>(keycode + 1);
}

// Convert beatoraja display position (0-6=keys, 7=scratch) → unified display lane (1-7=keys, 0=scratch).
constexpr int bev_display_to_unified(int display_bev) {
    return (display_bev == 7) ? 0 : display_bev + 1;
}

// Convert beatoraja BMS lane (0-6=keys, 7=scratch) → unified BMS lane (1-7=keys, 0=scratch).
constexpr int bev_bms_to_unified(int bms_bev) {
    return (bms_bev == 7) ? 0 : bms_bev + 1;
}

// Extract `laneShufflePattern[0][i]` from a new-style BRD JSON and convert
// it to the unified shuffle_pattern[unified_display_lane] = unified_bms_lane format.
// Returns true if a pattern was present (and at least partially valid).
bool extract_lane_shuffle_pattern_unified(const nlohmann::json& j,
                                            std::array<int, 8>& out) {
    if (!j.contains("laneShufflePattern") || !j["laneShufflePattern"].is_array()) {
        return false;
    }
    const auto& outer = j["laneShufflePattern"];
    if (outer.empty() || !outer[0].is_array() || outer[0].size() != 8) {
        return false;
    }

    int beatoraja_pattern[8] = {0, 1, 2, 3, 4, 5, 6, 7};
    for (int i = 0; i < 8; ++i) {
        if (outer[0][i].is_number_integer()) {
            beatoraja_pattern[i] = outer[0][i].get<int>();
        }
    }

    for (int display_bev = 0; display_bev < 8; ++display_bev) {
        const int bms_bev     = beatoraja_pattern[display_bev];
        const int display_our = bev_display_to_unified(display_bev);
        const int bms_our     = bev_bms_to_unified(bms_bev);
        out[display_our] = bms_our;
    }
    return true;
}

} // namespace

// --- New BRD (keyinput base64 + gzip) ---

std::optional<UnifiedReplay> parse_brd_new(const nlohmann::json& j, ParseError* error) {
    if (!j.contains("keyinput") || !j["keyinput"].is_string()) {
        set_error(error, "extract_events", "missing_field",
                  "new BRD: missing 'keyinput' string field");
        return std::nullopt;
    }

    const std::string keyinput = j["keyinput"].get<std::string>();
    auto decoded = base64::decode(keyinput);
    if (decoded.empty()) {
        set_error(error, "extract_events", "base64_decode_failed",
                  "new BRD: base64 decode of 'keyinput' produced empty result");
        return std::nullopt;
    }

    auto bin = gzip::decompress(decoded.data(), decoded.size());
    if (bin.empty()) {
        set_error(error, "extract_events", "gzip_decompress_failed",
                  "new BRD: gzip decompress of 'keyinput' payload produced empty result");
        return std::nullopt;
    }

    UnifiedReplay replay;
    int64_t last_ts = 0;

    const size_t frames = bin.size() / 9;
    for (size_t i = 0; i < frames; ++i) {
        const uint8_t* p = &bin[i * 9];

        const int8_t  val     = static_cast<int8_t>(p[0]);
        const bool    pressed = (val > 0);
        const int     keycode = std::abs(static_cast<int>(val)) - 1;
        if (keycode < 0 || keycode > 7) continue;

        int64_t ts = 0;
        std::memcpy(&ts, p + 1, 8);
        last_ts = ts;

        ReplayEvent ev{ts, keycode_to_lane(keycode)};
        if (pressed) {
            replay.press_events.push_back(ev);
        } else {
            replay.release_events.push_back(ev);
        }
    }

    replay.metadata.duration_us = last_ts;

    // Shuffle pattern (optional; default identity if missing)
    std::array<int, 8> pattern{};
    if (extract_lane_shuffle_pattern_unified(j, pattern)) {
        replay.metadata.shuffle_pattern = pattern;
    }

    // Seed (optional, useful for debugging)
    if (j.contains("randomoptionseed") && j["randomoptionseed"].is_number_integer()) {
        replay.metadata.seed = j["randomoptionseed"].get<int>();
    }

    return replay;
}

// --- Old BRD (keylog JSON array) ---

std::optional<UnifiedReplay> parse_brd_old(const nlohmann::json& j, ParseError* error) {
    if (!j.contains("keylog") || !j["keylog"].is_array()) {
        set_error(error, "extract_events", "missing_field",
                  "old BRD: missing 'keylog' array field");
        return std::nullopt;
    }

    UnifiedReplay replay;
    int64_t last_ts = 0;

    for (const auto& entry : j["keylog"]) {
        if (!entry.is_object()) continue;

        if (!entry.contains("presstime") || !entry["presstime"].is_number_integer()) {
            continue;
        }
        const int64_t ts = entry["presstime"].get<int64_t>();

        if (!entry.contains("keycode") || !entry["keycode"].is_number_integer()) {
            continue;
        }
        const int keycode = entry["keycode"].get<int>();
        if (keycode < 0 || keycode > 7) continue;

        bool pressed = false;
        if (entry.contains("pressed") && entry["pressed"].is_boolean()) {
            pressed = entry["pressed"].get<bool>();
        }

        last_ts = ts;

        ReplayEvent ev{ts, keycode_to_lane(keycode)};
        if (pressed) {
            replay.press_events.push_back(ev);
        } else {
            replay.release_events.push_back(ev);
        }
    }

    replay.metadata.duration_us = last_ts;

    // Shuffle pattern: only when random_option is 2 or 9
    if (j.contains("randomoption") && j["randomoption"].is_number_integer() &&
        j.contains("randomoptionseed") && j["randomoptionseed"].is_number_integer()) {
        const int random_option = j["randomoption"].get<int>();
        const int seed          = j["randomoptionseed"].get<int>();

        replay.metadata.seed = seed;

        if (random_option == 2 || random_option == 9) {
            replay.metadata.random_option =
                static_cast<RandomMode>(random_option);
            replay.metadata.shuffle_pattern =
                compute_shuffle_pattern(seed, RandomType::OldBRD, random_option);
        } else {
            replay.metadata.random_option =
                static_cast<RandomMode>(random_option);
        }
    }

    return replay;
}

} // namespace bmv
