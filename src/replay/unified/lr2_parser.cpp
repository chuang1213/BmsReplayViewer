#include "lr2_parser.h"
#include "random.h"

#include <array>
#include <cstdint>
#include <utility>

namespace bmv {

namespace {

// LR2 7-key 模式是默认按键数；LR2 标准布局就是 7K + 1 scratch。
constexpr int kLr2KeyCount = 7;

// Set error fields in a single call.
void set_error(ParseError* error, std::string stage, std::string reason, std::string context) {
    if (!error) return;
    error->stage   = std::move(stage);
    error->reason  = std::move(reason);
    error->context = std::move(context);
}

// Read little-endian integer from byte buffer.
template <typename T>
T read_le(const uint8_t* p) {
    T v = 0;
    for (size_t i = 0; i < sizeof(T); ++i)
        v |= static_cast<T>(p[i]) << (i * 8);
    return v;
}

// Map LR2 op (0-7) → unified display lane (0=scratch, 1-7=keys).
// Returns 0xFF if op is not a judged key.
uint8_t op_to_lane(int op) {
    if (op == 0)             return 0;
    if (op >= 1 && op <= 7)  return static_cast<uint8_t>(op);
    return 0xFF;
}

// Validate and cast a random_mode int → RandomMode. Returns nullopt if invalid.
std::optional<RandomMode> try_make_random_mode(int value) {
    if (value < 0 || value > 4) return std::nullopt;
    return static_cast<RandomMode>(value);
}

} // namespace

std::optional<UnifiedReplay> parse_lr2(const std::vector<uint8_t>& raw,
                                        ParseError* error) {
    const size_t size = raw.size();

    if (size == 0) {
        set_error(error, "read", "empty_input", "lr2rep: empty input buffer");
        return std::nullopt;
    }

    if (size % 12 != 0) {
        set_error(error, "extract_events", "invalid_size",
                  "lr2rep: file size " + std::to_string(size) +
                  " is not a multiple of 12 bytes (record size)");
        return std::nullopt;
    }

    UnifiedReplay replay;
    int64_t last_event_us = 0;

    // Temporary LR2-specific state, translated into UnifiedReplay metadata at the end.
    std::optional<RandomMode> p1_random;
    std::optional<RandomMode> p2_random;
    int                        seed    = 0;
    bool                       has_seed = false;
    std::vector<uint8_t>       judgements;

    const size_t n_records = size / 12;
    for (size_t i = 0; i < n_records; ++i) {
        const uint8_t* p = &raw[i * 12];
        const int32_t  time_ms = read_le<int32_t>(p + 0);
        const int32_t  op      = read_le<int32_t>(p + 4);
        const int32_t  value   = read_le<int32_t>(p + 8);

        if (op < 40) {
            const uint8_t lane = op_to_lane(op);
            if (lane > 7) continue;

            const int64_t time_us = static_cast<int64_t>(time_ms) * 1000;
            const bool    down    = (value == 1);

            last_event_us = time_us;
            if (down) {
                replay.press_events.push_back(ReplayEvent{time_us, lane});
            } else {
                replay.release_events.push_back(ReplayEvent{time_us, lane});
            }
        } else {
            switch (op) {
            case 103:
                p1_random = try_make_random_mode(value);
                break;
            case 153:
                p2_random = try_make_random_mode(value);
                break;
            case 200:
                seed     = value;
                has_seed = true;
                break;
            case 210:
                judgements.push_back(static_cast<uint8_t>(value));
                break;
            default:
                break;
            }
        }
    }

    replay.metadata.duration_us = last_event_us;

    // --- Metadata: random_option (P1 takes priority over P2) ---
    if (p1_random.has_value()) {
        replay.metadata.random_option = p1_random;
    } else if (p2_random.has_value()) {
        replay.metadata.random_option = p2_random;
    }

    // --- Metadata: seed (only when explicitly set via op 200) ---
    if (has_seed) {
        replay.metadata.seed = seed;
    }

    // --- Metadata: judgements (op 210) ---
    if (!judgements.empty()) {
        replay.metadata.judgements = std::move(judgements);
    }

    // --- Metadata: shuffle_pattern (only when random mode needs it) ---
    if (replay.metadata.random_option.has_value()) {
        const RandomMode mode = *replay.metadata.random_option;
        const bool       needs_shuffle =
            (mode == RandomMode::Random || mode == RandomMode::RRandom);

        if (needs_shuffle && has_seed) {
            replay.metadata.shuffle_pattern =
                compute_shuffle_pattern(seed, RandomType::LR2, kLr2KeyCount);
        }
        // Mirror / SRandom / Off → keep identity (default)
    }

    return replay;
}

} // namespace bmv
