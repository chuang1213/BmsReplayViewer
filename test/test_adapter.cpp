// 单元测试：验证 unified/adapter
//   - unified_to_replay_data()  将 UnifiedReplay → ReplayData
//   - 旧 adapter (replay_input_to_replay_data) 将 ReplayInput → ReplayData
//   - 同一份原始数据，新旧两条路径产生的 hits 必须数量一致、按
//     (tick_start, tick_end, lane, raw_keycode) 排序后逐项相等。
//
// 用例：
//   1. testfiles/sp/..._088_Normal.brd           (新 BRD)
//   2. testfiles/086/..._1.brd                   (旧 BRD, random=2)
//   3. testfiles/sp/eeff3a9a7cd557a05308ba1296752db2.lr2rep
//   4. 合成 BRD UnifiedReplay：单次 press+release
//   5. 合成 LR2 UnifiedReplay：press + release + 元数据
//   6. 合成 BRD：EOF 时仍有 active key → unmatched 计数

#include "replay/unified/adapter.h"
#include "replay/unified/parser.h"
#include "replay/unified/types.h"
#include "replay/replay_adapter.h"
#include "replay/replay_data.h"
#include "replay/raw_input_event.h"
#include "replay/brd_parser.h"
#include "replay/lr2rep_parser.h"
#include "core/time_map.h"
#include "core/types.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace {

const char* test_result(bool ok) { return ok ? "PASS" : "FAIL"; }

void make_identity_timemap(bmv::TimeMap& tm) {
    std::vector<bmv::BpmEvent> bpms;
    std::vector<bmv::StopEvent> stops;
    // 120 BPM, no stops → simple linear mapping (1 second = 3840 ticks).
    tm.build(bpms, stops, 120.0);
}

bool hit_eq(const bmv::ReplayHit& a, const bmv::ReplayHit& b) {
    return a.tick_start  == b.tick_start  &&
           a.tick_end    == b.tick_end    &&
           a.lane        == b.lane        &&
           a.raw_keycode == b.raw_keycode;
}

void sort_hits(std::vector<bmv::ReplayHit>& hits) {
    std::sort(hits.begin(), hits.end(),
              [](const bmv::ReplayHit& a, const bmv::ReplayHit& b) {
                  if (a.tick_start  != b.tick_start)  return a.tick_start  < b.tick_start;
                  if (a.tick_end    != b.tick_end)    return a.tick_end    < b.tick_end;
                  if (a.lane        != b.lane)        return a.lane        < b.lane;
                  return a.raw_keycode < b.raw_keycode;
              });
}

// Build a ReplayInput from a UnifiedReplay by flattening press + release
// streams. This is the inverse of what the unified parsers do, and lets us
// route a UnifiedReplay through the OLD adapter to cross-check.
bmv::ReplayInput replay_input_from_unified(const bmv::UnifiedReplay& ur,
                                            bmv::ReplayFormat fmt) {
    bmv::ReplayInput in;
    in.format      = fmt;
    in.duration_us = ur.metadata.duration_us;

    struct M { int64_t time_us; uint8_t lane; bool down; };
    std::vector<M> merged;
    merged.reserve(ur.press_events.size() + ur.release_events.size());
    for (const auto& e : ur.press_events) merged.push_back({e.time_us, e.lane, true});
    for (const auto& e : ur.release_events) merged.push_back({e.time_us, e.lane, false});
    std::stable_sort(merged.begin(), merged.end(),
                     [](const M& a, const M& b) {
                         if (a.time_us != b.time_us) return a.time_us < b.time_us;
                         return a.down > b.down;
                     });
    in.events.reserve(merged.size());
    for (const auto& m : merged) {
        in.events.push_back({m.time_us, m.lane, m.down});
    }

    if (fmt == bmv::ReplayFormat::BRD) {
        // Unified parser always populates shuffle_pattern; has_shuffle
        // is implicit (non-identity means a real pattern was found).
        std::array<int, 8> identity = {0,1,2,3,4,5,6,7};
        for (int i = 0; i < 8; ++i)
            in.brd.shuffle_pattern[i] = ur.metadata.shuffle_pattern[i];
        for (int i = 0; i < 8; ++i) {
            if (in.brd.shuffle_pattern[i] != identity[i]) {
                in.brd.has_shuffle = true;
                break;
            }
        }
    } else {
        if (ur.metadata.random_option.has_value()) {
            in.lr2.has_random_info[0] = true;
            in.lr2.random_mode[0] =
                static_cast<bmv::LR2RandomMode>(static_cast<int>(*ur.metadata.random_option));
        }
        if (ur.metadata.seed.has_value()) {
            in.lr2.seed = *ur.metadata.seed;
        }
        if (ur.metadata.judgements.has_value()) {
            in.lr2.op210 = *ur.metadata.judgements;
        }
    }
    return in;
}

} // namespace

// ---------- 1. BRD 新版 (keyinput) ----------

static bool test_brd_new_real() {
    std::printf("=== test_brd_new_real ===\n");
    const std::string path = "testfiles/sp/"
        "bc1462814a47c6a0e2d6a6779014f78e8f6f94c3b49ab6ecb9a5752d18137e16_088_Normal.brd";

    bmv::ParseError err;
    auto ur = bmv::parse_replay(path, &err);
    if (!ur) {
        std::printf("  FAIL: parse_replay failed: %s/%s\n", err.stage.c_str(), err.reason.c_str());
        return false;
    }

    bmv::BrdParser p;
    auto legacy_in = p.parse_replay_input(path);

    bmv::TimeMap tm;
    make_identity_timemap(tm);

    auto new_data = bmv::unified_to_replay_data(*ur, tm, bmv::ReplayFormat::BRD);
    auto old_data = bmv::replay_input_to_replay_data(legacy_in, tm);

    std::printf("  unified: hits=%zu  duration_us=%lld  has_shuffle=%d  unmatched=%d\n",
                new_data.hits.size(),
                static_cast<long long>(new_data.duration_us),
                static_cast<int>(new_data.has_shuffle),
                new_data.unmatched);
    std::printf("  legacy : hits=%zu  duration_us=%lld  has_shuffle=%d  unmatched=%d\n",
                old_data.hits.size(),
                static_cast<long long>(old_data.duration_us),
                static_cast<int>(old_data.has_shuffle),
                old_data.unmatched);

    if (new_data.hits.size() != old_data.hits.size()) {
        std::printf("  FAIL: hit count mismatch (new=%zu, old=%zu)\n",
                    new_data.hits.size(), old_data.hits.size());
        return false;
    }

    sort_hits(new_data.hits);
    sort_hits(old_data.hits);
    for (size_t i = 0; i < new_data.hits.size(); ++i) {
        if (!hit_eq(new_data.hits[i], old_data.hits[i])) {
            std::printf("  FAIL: hit[%zu] mismatch\n  new=", i);
            const auto& h = new_data.hits[i];
            std::printf("(tick_start=%lld, tick_end=%lld, lane=%u, raw_keycode=%u, is_press=%d)\n  old=",
                        static_cast<long long>(h.tick_start),
                        static_cast<long long>(h.tick_end),
                        h.lane, h.raw_keycode, static_cast<int>(h.is_press));
            const auto& g = old_data.hits[i];
            std::printf("(tick_start=%lld, tick_end=%lld, lane=%u, raw_keycode=%u, is_press=%d)\n",
                        static_cast<long long>(g.tick_start),
                        static_cast<long long>(g.tick_end),
                        g.lane, g.raw_keycode, static_cast<int>(g.is_press));
            return false;
        }
    }

    if (new_data.duration_us != old_data.duration_us) {
        std::printf("  FAIL: duration_us mismatch (new=%lld, old=%lld)\n",
                    static_cast<long long>(new_data.duration_us),
                    static_cast<long long>(old_data.duration_us));
        return false;
    }
    if (new_data.unmatched != old_data.unmatched) {
        std::printf("  FAIL: unmatched mismatch (new=%d, old=%d)\n",
                    new_data.unmatched, old_data.unmatched);
        return false;
    }

    std::printf("  result: %s\n\n", test_result(true));
    return true;
}

// ---------- 2. BRD 旧版 (keylog, random) ----------

static bool test_brd_old_real() {
    std::printf("=== test_brd_old_real ===\n");
    const std::string path = "testfiles/086/"
        "086_random_bc1462814a47c6a0e2d6a6779014f78e8f6f94c3b49ab6ecb9a5752d18137e16_1.brd";

    bmv::ParseError err;
    auto ur = bmv::parse_replay(path, &err);
    if (!ur) {
        std::printf("  FAIL: parse_replay failed: %s/%s\n", err.stage.c_str(), err.reason.c_str());
        return false;
    }

    bmv::BrdParser p;
    auto legacy_in = p.parse_replay_input(path);

    bmv::TimeMap tm;
    make_identity_timemap(tm);

    auto new_data = bmv::unified_to_replay_data(*ur, tm, bmv::ReplayFormat::BRD);
    auto old_data = bmv::replay_input_to_replay_data(legacy_in, tm);

    std::printf("  unified: hits=%zu  has_shuffle=%d\n",
                new_data.hits.size(), static_cast<int>(new_data.has_shuffle));
    std::printf("  legacy : hits=%zu  has_shuffle=%d\n",
                old_data.hits.size(), static_cast<int>(old_data.has_shuffle));

    if (new_data.hits.size() != old_data.hits.size()) {
        std::printf("  FAIL: hit count mismatch (new=%zu, old=%zu)\n",
                    new_data.hits.size(), old_data.hits.size());
        return false;
    }
    if (new_data.has_shuffle != old_data.has_shuffle) {
        std::printf("  FAIL: has_shuffle mismatch (new=%d, old=%d)\n",
                    static_cast<int>(new_data.has_shuffle),
                    static_cast<int>(old_data.has_shuffle));
        return false;
    }
    for (int i = 0; i < 8; ++i) {
        if (new_data.shuffle_pattern[i] != old_data.shuffle_pattern[i]) {
            std::printf("  FAIL: shuffle_pattern[%d] mismatch (new=%d, old=%d)\n",
                        i, new_data.shuffle_pattern[i], old_data.shuffle_pattern[i]);
            return false;
        }
    }

    sort_hits(new_data.hits);
    sort_hits(old_data.hits);
    for (size_t i = 0; i < new_data.hits.size(); ++i) {
        if (!hit_eq(new_data.hits[i], old_data.hits[i])) {
            std::printf("  FAIL: hit[%zu] mismatch\n", i);
            return false;
        }
    }

    std::printf("  result: %s\n\n", test_result(true));
    return true;
}

// ---------- 3. LR2 ----------

static bool test_lr2_real() {
    std::printf("=== test_lr2_real ===\n");
    const std::string path = "testfiles/sp/eeff3a9a7cd557a05308ba1296752db2.lr2rep";

    bmv::ParseError err;
    auto ur = bmv::parse_replay(path, &err);
    if (!ur) {
        std::printf("  FAIL: parse_replay failed: %s/%s\n", err.stage.c_str(), err.reason.c_str());
        return false;
    }

    bmv::Lr2RepParser p;
    auto legacy_in = p.parse_replay_input(path);

    bmv::TimeMap tm;
    make_identity_timemap(tm);

    auto new_data = bmv::unified_to_replay_data(*ur, tm, bmv::ReplayFormat::LR2REP);
    auto old_data = bmv::replay_input_to_replay_data(legacy_in, tm);

    std::printf("  unified: hits=%zu  duration_us=%lld  has_random_info[0]=%d  random_seed=%d  lr2_judgements=%zu\n",
                new_data.hits.size(),
                static_cast<long long>(new_data.duration_us),
                static_cast<int>(new_data.has_random_info[0]),
                new_data.random_seed,
                new_data.lr2_judgements.size());
    std::printf("  legacy : hits=%zu  duration_us=%lld  has_random_info[0]=%d  random_seed=%d  lr2_judgements=%zu\n",
                old_data.hits.size(),
                static_cast<long long>(old_data.duration_us),
                static_cast<int>(old_data.has_random_info[0]),
                old_data.random_seed,
                old_data.lr2_judgements.size());

    if (new_data.hits.size() != old_data.hits.size()) {
        std::printf("  FAIL: hit count mismatch (new=%zu, old=%zu)\n",
                    new_data.hits.size(), old_data.hits.size());
        return false;
    }
    if (new_data.lr2_judgements.size() != old_data.lr2_judgements.size()) {
        std::printf("  FAIL: lr2_judgements size mismatch\n");
        return false;
    }
    for (size_t i = 0; i < new_data.lr2_judgements.size(); ++i) {
        if (new_data.lr2_judgements[i] != old_data.lr2_judgements[i]) {
            std::printf("  FAIL: lr2_judgements[%zu] mismatch\n", i);
            return false;
        }
    }
    if (new_data.has_random_info[0] != old_data.has_random_info[0] ||
        new_data.random_mode[0]     != old_data.random_mode[0]     ||
        new_data.random_seed        != old_data.random_seed) {
        std::printf("  FAIL: LR2 metadata mismatch\n");
        return false;
    }

    sort_hits(new_data.hits);
    sort_hits(old_data.hits);
    for (size_t i = 0; i < new_data.hits.size(); ++i) {
        if (!hit_eq(new_data.hits[i], old_data.hits[i])) {
            std::printf("  FAIL: hit[%zu] mismatch\n", i);
            return false;
        }
    }

    std::printf("  result: %s\n\n", test_result(true));
    return true;
}

// ---------- 4. 合成 BRD：单次 press+release ----------

static bool test_synthetic_brd_simple() {
    std::printf("=== test_synthetic_brd_simple ===\n");
    // 120 BPM, no stops → 1 second = 3840 ticks.
    // Press lane 3 at 0.5s (= 1920 ticks), release at 1.0s (= 3840 ticks).
    // Expected hit: tick_start=1920, tick_end=3840, lane=3, raw_keycode=2.
    bmv::UnifiedReplay ur;
    ur.press_events.push_back({500'000, 3});
    ur.release_events.push_back({1'000'000, 3});
    ur.metadata.duration_us = 1'000'000;

    bmv::TimeMap tm;
    make_identity_timemap(tm);

    auto data = bmv::unified_to_replay_data(ur, tm, bmv::ReplayFormat::BRD);

    std::printf("  hits=%zu  unmatched=%d\n", data.hits.size(), data.unmatched);
    if (data.hits.size() != 1) {
        std::printf("  FAIL: expected 1 hit, got %zu\n", data.hits.size());
        return false;
    }
    if (data.unmatched != 0) {
        std::printf("  FAIL: expected unmatched=0\n");
        return false;
    }
    const auto& h = data.hits[0];
    if (h.tick_start != 1920 || h.tick_end != 3840) {
        std::printf("  FAIL: tick mismatch (got [%lld, %lld], want [1920, 3840])\n",
                    static_cast<long long>(h.tick_start),
                    static_cast<long long>(h.tick_end));
        return false;
    }
    if (h.lane != 3 || h.raw_keycode != 2) {
        std::printf("  FAIL: lane/keycode mismatch (lane=%u, raw=%u)\n",
                    h.lane, h.raw_keycode);
        return false;
    }
    if (!h.is_press) {
        std::printf("  FAIL: is_press should be true for paired hit\n");
        return false;
    }
    if (data.duration_us != 1'000'000) {
        std::printf("  FAIL: duration_us mismatch\n");
        return false;
    }
    std::printf("  result: %s\n\n", test_result(true));
    return true;
}

// ---------- 5. 合成 BRD：EOF 时 active → unmatched ----------

static bool test_synthetic_brd_unmatched_eof() {
    std::printf("=== test_synthetic_brd_unmatched_eof ===\n");
    bmv::UnifiedReplay ur;
    // Scratch (lane 0) press, no release.
    ur.press_events.push_back({100'000, 0});
    // Lane 5 press + release, complete pair.
    ur.press_events.push_back({200'000, 5});
    ur.release_events.push_back({300'000, 5});
    ur.metadata.duration_us = 300'000;

    bmv::TimeMap tm;
    make_identity_timemap(tm);

    auto data = bmv::unified_to_replay_data(ur, tm, bmv::ReplayFormat::BRD);

    std::printf("  hits=%zu  unmatched=%d\n", data.hits.size(), data.unmatched);
    if (data.hits.size() != 2) {
        std::printf("  FAIL: expected 2 hits, got %zu\n", data.hits.size());
        return false;
    }
    if (data.unmatched != 1) {
        std::printf("  FAIL: expected unmatched=1, got %d\n", data.unmatched);
        return false;
    }

    // Scratch hit must be the unmatched one: lane=0, raw_keycode=7.
    int scratch_idx = -1, key5_idx = -1;
    for (size_t i = 0; i < data.hits.size(); ++i) {
        if (data.hits[i].lane == 0) scratch_idx = static_cast<int>(i);
        if (data.hits[i].lane == 5) key5_idx   = static_cast<int>(i);
    }
    if (scratch_idx < 0 || key5_idx < 0) {
        std::printf("  FAIL: missing expected lanes\n");
        return false;
    }
    if (data.hits[scratch_idx].raw_keycode != 7) {
        std::printf("  FAIL: scratch raw_keycode should be 7\n");
        return false;
    }
    if (data.hits[key5_idx].raw_keycode != 4) {
        std::printf("  FAIL: lane 5 raw_keycode should be 4\n");
        return false;
    }
    // EOF cutoff tick: last processed tick is at 300'000us = 300'000 * 3840 / 1'000'000 = 1152.
    const bmv::tick_t expected_last = 1152;
    if (data.hits[scratch_idx].tick_end != expected_last) {
        std::printf("  FAIL: scratch EOF tick_end should be %lld, got %lld\n",
                    static_cast<long long>(expected_last),
                    static_cast<long long>(data.hits[scratch_idx].tick_end));
        return false;
    }

    std::printf("  result: %s\n\n", test_result(true));
    return true;
}

// ---------- 6. 合成 LR2：press + release + 元数据 ----------

static bool test_synthetic_lr2_with_metadata() {
    std::printf("=== test_synthetic_lr2_with_metadata ===\n");
    bmv::UnifiedReplay ur;
    ur.press_events.push_back({1'000'000, 5}); // lane 5 (LR2 keys)
    ur.release_events.push_back({2'000'000, 5});
    ur.press_events.push_back({3'000'000, 0}); // scratch
    ur.release_events.push_back({4'000'000, 0});
    ur.metadata.duration_us  = 4'000'000;
    ur.metadata.random_option = bmv::RandomMode::Random;
    ur.metadata.seed          = 42;
    std::vector<uint8_t> judgements = {1, 2, 1, 0};
    ur.metadata.judgements    = judgements;

    bmv::TimeMap tm;
    make_identity_timemap(tm);

    auto data = bmv::unified_to_replay_data(ur, tm, bmv::ReplayFormat::LR2REP);

    std::printf("  hits=%zu  has_random_info[0]=%d  random_mode[0]=%d  random_seed=%d  lr2_judgements=%zu\n",
                data.hits.size(),
                static_cast<int>(data.has_random_info[0]),
                static_cast<int>(data.random_mode[0]),
                data.random_seed,
                data.lr2_judgements.size());

    if (data.hits.size() != 4) {
        std::printf("  FAIL: expected 4 hits, got %zu\n", data.hits.size());
        return false;
    }
    if (!data.has_random_info[0]) {
        std::printf("  FAIL: has_random_info[0] should be true\n");
        return false;
    }
    if (data.random_mode[0] != bmv::LR2RandomMode::Random) {
        std::printf("  FAIL: random_mode[0] should be Random\n");
        return false;
    }
    if (data.random_seed != 42) {
        std::printf("  FAIL: random_seed should be 42\n");
        return false;
    }
    if (data.lr2_judgements != judgements) {
        std::printf("  FAIL: lr2_judgements mismatch\n");
        return false;
    }
    // Each hit: tick_start == tick_end, is_press reflects stream origin.
    for (size_t i = 0; i < data.hits.size(); ++i) {
        const auto& h = data.hits[i];
        if (h.tick_start != h.tick_end) {
            std::printf("  FAIL: hit[%zu] tick_start != tick_end\n", i);
            return false;
        }
        if (h.raw_keycode != h.lane) {
            std::printf("  FAIL: hit[%zu] raw_keycode != lane\n", i);
            return false;
        }
    }
    // Order: time-ascending → press(5,1s), release(5,2s), press(0,3s), release(0,4s).
    const bmv::tick_t expected_ticks[4] = {3840, 7680, 11520, 15360};
    for (int i = 0; i < 4; ++i) {
        if (data.hits[i].tick_start != expected_ticks[i]) {
            std::printf("  FAIL: hit[%d] tick should be %lld, got %lld\n",
                        i, static_cast<long long>(expected_ticks[i]),
                        static_cast<long long>(data.hits[i].tick_start));
            return false;
        }
    }
    if (!data.hits[0].is_press || !data.hits[2].is_press) {
        std::printf("  FAIL: press/release flag wrong\n");
        return false;
    }
    if (data.hits[1].is_press || data.hits[3].is_press) {
        std::printf("  FAIL: press/release flag wrong\n");
        return false;
    }

    std::printf("  result: %s\n\n", test_result(true));
    return true;
}

// ---------- 7. 新旧 adapter 端到端等价性（合成数据） ----------

static bool test_synthetic_equivalence() {
    std::printf("=== test_synthetic_equivalence ===\n");
    bmv::UnifiedReplay ur;
    ur.press_events.push_back({500'000, 0});
    ur.release_events.push_back({1'500'000, 0});
    ur.press_events.push_back({1'000'000, 3});
    ur.release_events.push_back({2'000'000, 3});
    ur.metadata.duration_us = 2'000'000;
    std::array<int, 8> pattern = {0, 3, 7, 6, 1, 2, 5, 4};
    ur.metadata.shuffle_pattern = pattern;
    ur.metadata.random_option = bmv::RandomMode::Random;
    ur.metadata.seed = 12345;
    std::vector<uint8_t> judgements = {1, 1, 0, 1};
    ur.metadata.judgements = judgements;

    bmv::TimeMap tm;
    make_identity_timemap(tm);

    auto brd_new = bmv::unified_to_replay_data(ur, tm, bmv::ReplayFormat::BRD);
    auto lr2_new = bmv::unified_to_replay_data(ur, tm, bmv::ReplayFormat::LR2REP);

    auto in = replay_input_from_unified(ur, bmv::ReplayFormat::BRD);
    // Patch in with BRD shuffle flag (done inside helper).
    auto brd_old = bmv::replay_input_to_replay_data(in, tm);

    std::printf("  BRD new: hits=%zu\n", brd_new.hits.size());
    std::printf("  BRD old: hits=%zu\n", brd_old.hits.size());
    std::printf("  LR2 new: hits=%zu\n", lr2_new.hits.size());

    if (brd_new.hits.size() != brd_old.hits.size()) {
        std::printf("  FAIL: BRD new/old hit count mismatch\n");
        return false;
    }
    if (brd_new.hits.size() != 2) {
        std::printf("  FAIL: expected 2 BRD hits, got %zu\n", brd_new.hits.size());
        return false;
    }
    if (lr2_new.hits.size() != 4) {
        std::printf("  FAIL: expected 4 LR2 hits, got %zu\n", lr2_new.hits.size());
        return false;
    }
    if (brd_new.has_shuffle != true) {
        std::printf("  FAIL: BRD has_shuffle should be true\n");
        return false;
    }
    for (int i = 0; i < 8; ++i) {
        if (brd_new.shuffle_pattern[i] != pattern[i]) {
            std::printf("  FAIL: BRD shuffle_pattern[%d] mismatch\n", i);
            return false;
        }
    }

    std::printf("  result: %s\n\n", test_result(true));
    return true;
}

int main() {
    bool all_pass = true;

    all_pass &= test_brd_new_real();
    all_pass &= test_brd_old_real();
    all_pass &= test_lr2_real();
    all_pass &= test_synthetic_brd_simple();
    all_pass &= test_synthetic_brd_unmatched_eof();
    all_pass &= test_synthetic_lr2_with_metadata();
    all_pass &= test_synthetic_equivalence();

    std::printf("========================================\n");
    std::printf("%s\n", all_pass ? "所有测试通过!" : "部分测试失败!");
    std::printf("========================================\n");
    return all_pass ? 0 : 1;
}
