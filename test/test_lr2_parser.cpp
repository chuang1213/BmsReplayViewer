// 单元测试：验证 unified/lr2_parser
//   - parse_lr2: 12 字节记录的 LR2REP 格式解析
//   - 与现有 Lr2RepParser 输出的一致性
//   - shuffle_pattern 计算正确性
//   - 错误情况返回结构化 ParseError
//
// 用例：
//   1. testfiles/sp/eeff3a9a7cd557a05308ba1296752db2.lr2rep  (no random, off mode)
//   2. testfiles/sp/random1.lr2rep                            (Random mode, seed=67)
//   3. testfiles/sp/random2.lr2rep                            (Random mode, seed=2746)
//
// 验证项：
//   - 解析成功
//   - 事件数 / lane / 时间戳正确
//   - shuffle_pattern 正确还原 (off=identity, random=compute_shuffle_pattern)
//   - 与现有 lr2rep_parser 输出一致
//   - 错误情况 (空 / 非 12 倍数) 返回结构化 ParseError

#include "replay/unified/lr2_parser.h"
#include "replay/unified/random.h"
#include "replay/lr2rep_parser.h"
#include "replay/raw_input_event.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

namespace {

// ---------- 辅助函数 ----------

std::vector<uint8_t> read_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return {};
    const size_t size = static_cast<size_t>(f.tellg());
    f.seekg(0, std::ios::beg);
    std::vector<uint8_t> data(size);
    f.read(reinterpret_cast<char*>(data.data()), size);
    return data;
}

void print_pattern(const char* tag, const std::array<int, 8>& p) {
    std::printf("  %s: %d %d %d %d %d %d %d %d\n", tag,
                p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7]);
}

bool pattern_eq(const std::array<int, 8>& a, const std::array<int, 8>& b) {
    for (int i = 0; i < 8; ++i) if (a[i] != b[i]) return false;
    return true;
}

const char* test_result(bool ok) { return ok ? "PASS" : "FAIL"; }

// 遍历统一格式事件的总和 (press + release)
size_t total_events(const bmv::UnifiedReplay& r) {
    return r.press_events.size() + r.release_events.size();
}

} // namespace

// ---------- 1. 普通 LR2 (Off 模式) ----------

static bool test_normal_no_random() {
    std::printf("=== test_normal_no_random ===\n");
    const std::string path = "testfiles/sp/eeff3a9a7cd557a05308ba1296752db2.lr2rep";
    auto raw = read_file(path);
    if (raw.empty()) {
        std::printf("  FAIL: cannot read %s\n", path.c_str());
        return false;
    }

    auto r = bmv::parse_lr2(raw);
    if (!r) {
        std::printf("  FAIL: parse_lr2 returned nullopt\n");
        return false;
    }

    std::printf("  press=%zu  release=%zu  total=%zu\n",
                r->press_events.size(), r->release_events.size(), total_events(*r));
    std::printf("  duration_us=%lld\n", static_cast<long long>(r->metadata.duration_us));

    // 实际数据: 2272 press + 2273 release = 4545 事件
    if (r->press_events.size() != 2272 || r->release_events.size() != 2273) {
        std::printf("  FAIL: event count mismatch\n");
        return false;
    }

    // duration = 143137000 us
    if (r->metadata.duration_us != 143137000) {
        std::printf("  FAIL: duration_us mismatch (got %lld, want 143137000)\n",
                    static_cast<long long>(r->metadata.duration_us));
        return false;
    }

    // random_option = Off
    if (!r->metadata.random_option.has_value() ||
        *r->metadata.random_option != bmv::RandomMode::Off) {
        std::printf("  FAIL: expected random_option=Off\n");
        return false;
    }

    // shuffle_pattern 应保持恒等
    const std::array<int, 8> identity = {0, 1, 2, 3, 4, 5, 6, 7};
    if (!pattern_eq(r->metadata.shuffle_pattern, identity)) {
        std::printf("  FAIL: shuffle_pattern should be identity for Off mode\n");
        print_pattern("got", r->metadata.shuffle_pattern);
        return false;
    }

    // seed 应被记录
    if (!r->metadata.seed.has_value() || *r->metadata.seed != 14272) {
        std::printf("  FAIL: expected seed=14272\n");
        return false;
    }

    // judgements 应有 2284 条
    if (!r->metadata.judgements.has_value() ||
        r->metadata.judgements->size() != 2284) {
        std::printf("  FAIL: expected judgements.size()=2284\n");
        return false;
    }

    std::printf("  result: %s\n\n", test_result(true));
    return true;
}

// ---------- 2. Random 模式 seed=67 ----------

static bool test_random1_seed_67() {
    std::printf("=== test_random1_seed_67 ===\n");
    const std::string path = "testfiles/sp/random1.lr2rep";
    auto raw = read_file(path);
    if (raw.empty()) {
        std::printf("  FAIL: cannot read %s\n", path.c_str());
        return false;
    }

    auto r = bmv::parse_lr2(raw);
    if (!r) {
        std::printf("  FAIL: parse_lr2 returned nullopt\n");
        return false;
    }

    std::printf("  press=%zu  release=%zu  total=%zu\n",
                r->press_events.size(), r->release_events.size(), total_events(*r));

    // 2265 press + 2270 release = 4535
    if (r->press_events.size() != 2265 || r->release_events.size() != 2270) {
        std::printf("  FAIL: event count mismatch\n");
        return false;
    }

    // random_option = Random(2)
    if (!r->metadata.random_option.has_value() ||
        *r->metadata.random_option != bmv::RandomMode::Random) {
        std::printf("  FAIL: expected random_option=Random\n");
        return false;
    }

    // shuffle_pattern 通过 compute_shuffle_pattern 独立验证
    const auto expected =
        bmv::compute_shuffle_pattern(67, bmv::RandomType::LR2, 7);
    if (!pattern_eq(r->metadata.shuffle_pattern, expected)) {
        std::printf("  FAIL: shuffle_pattern mismatch\n");
        print_pattern("got     ", r->metadata.shuffle_pattern);
        print_pattern("expected", expected);
        return false;
    }

    // 额外断言: shuffle_pattern 至少跟 identity 不同 (即确实经过计算)
    const std::array<int, 8> identity = {0, 1, 2, 3, 4, 5, 6, 7};
    if (pattern_eq(r->metadata.shuffle_pattern, identity)) {
        std::printf("  FAIL: shuffle_pattern should differ from identity for Random mode\n");
        return false;
    }

    if (!r->metadata.seed.has_value() || *r->metadata.seed != 67) {
        std::printf("  FAIL: expected seed=67\n");
        return false;
    }

    if (!r->metadata.judgements.has_value() ||
        r->metadata.judgements->size() != 2276) {
        std::printf("  FAIL: expected judgements.size()=2276\n");
        return false;
    }

    std::printf("  result: %s\n\n", test_result(true));
    return true;
}

// ---------- 3. Random 模式 seed=2746 ----------

static bool test_random2_seed_2746() {
    std::printf("=== test_random2_seed_2746 ===\n");
    const std::string path = "testfiles/sp/random2.lr2rep";
    auto raw = read_file(path);
    if (raw.empty()) {
        std::printf("  FAIL: cannot read %s\n", path.c_str());
        return false;
    }

    auto r = bmv::parse_lr2(raw);
    if (!r) {
        std::printf("  FAIL: parse_lr2 returned nullopt\n");
        return false;
    }

    std::printf("  press=%zu  release=%zu  total=%zu\n",
                r->press_events.size(), r->release_events.size(), total_events(*r));

    if (r->press_events.size() != 2262 || r->release_events.size() != 2262) {
        std::printf("  FAIL: event count mismatch\n");
        return false;
    }

    if (!r->metadata.random_option.has_value() ||
        *r->metadata.random_option != bmv::RandomMode::Random) {
        std::printf("  FAIL: expected random_option=Random\n");
        return false;
    }

    const auto expected =
        bmv::compute_shuffle_pattern(2746, bmv::RandomType::LR2, 7);
    if (!pattern_eq(r->metadata.shuffle_pattern, expected)) {
        std::printf("  FAIL: shuffle_pattern mismatch\n");
        print_pattern("got     ", r->metadata.shuffle_pattern);
        print_pattern("expected", expected);
        return false;
    }

    if (!r->metadata.seed.has_value() || *r->metadata.seed != 2746) {
        std::printf("  FAIL: expected seed=2746\n");
        return false;
    }

    std::printf("  result: %s\n\n", test_result(true));
    return true;
}

// ---------- 4. 与现有 lr2rep_parser 输出一致 ----------

static bool test_consistency_with_existing() {
    std::printf("=== test_consistency_with_existing ===\n");
    const std::string path = "testfiles/sp/random1.lr2rep";
    auto raw = read_file(path);
    if (raw.empty()) {
        std::printf("  FAIL: cannot read %s\n", path.c_str());
        return false;
    }

    // 旧解析器
    bmv::Lr2RepParser legacy;
    bmv::ReplayInput  legacy_input = legacy.parse_replay_input(path);

    // 新解析器
    auto unified = bmv::parse_lr2(raw);
    if (!unified) {
        std::printf("  FAIL: parse_lr2 returned nullopt\n");
        return false;
    }

    // 事件总数应一致
    const size_t legacy_total  = legacy_input.events.size();
    const size_t unified_total = total_events(*unified);
    std::printf("  legacy events=%zu  unified press=%zu release=%zu (total=%zu)\n",
                legacy_total,
                unified->press_events.size(),
                unified->release_events.size(),
                unified_total);

    if (legacy_total != unified_total) {
        std::printf("  FAIL: total event count mismatch\n");
        return false;
    }

    // 按时间-顺序逐项比对: legacy 的 down=true → press, down=false → release
    size_t press_idx = 0;
    size_t release_idx = 0;
    for (size_t i = 0; i < legacy_input.events.size(); ++i) {
        const auto& le = legacy_input.events[i];
        const bmv::ReplayEvent* ue = nullptr;
        if (le.down) {
            if (press_idx >= unified->press_events.size()) {
                std::printf("  FAIL: press index out of range at i=%zu\n", i);
                return false;
            }
            ue = &unified->press_events[press_idx++];
        } else {
            if (release_idx >= unified->release_events.size()) {
                std::printf("  FAIL: release index out of range at i=%zu\n", i);
                return false;
            }
            ue = &unified->release_events[release_idx++];
        }
        if (ue->time_us != le.time_us || ue->lane != le.lane) {
            std::printf("  FAIL: event mismatch at i=%zu (legacy time=%lld lane=%u, "
                        "unified time=%lld lane=%u, down=%d)\n",
                        i, static_cast<long long>(le.time_us), le.lane,
                        static_cast<long long>(ue->time_us), ue->lane, le.down);
            return false;
        }
    }

    // 元数据: seed / random_mode / op210 应一致
    if (legacy_input.lr2.seed != 67) {
        std::printf("  FAIL: legacy seed should be 67\n");
        return false;
    }
    if (!unified->metadata.seed.has_value() || *unified->metadata.seed != legacy_input.lr2.seed) {
        std::printf("  FAIL: seed mismatch\n");
        return false;
    }
    if (!legacy_input.lr2.has_random_info[0] ||
        legacy_input.lr2.random_mode[0] != bmv::LR2RandomMode::Random) {
        std::printf("  FAIL: legacy P1 random mode should be Random(2)\n");
        return false;
    }
    if (!unified->metadata.random_option.has_value() ||
        *unified->metadata.random_option != bmv::RandomMode::Random) {
        std::printf("  FAIL: random_option mismatch\n");
        return false;
    }
    if (legacy_input.lr2.op210.size() != unified->metadata.judgements->size()) {
        std::printf("  FAIL: judgements count mismatch\n");
        return false;
    }

    std::printf("  result: %s\n\n", test_result(true));
    return true;
}

// ---------- 5. 错误情况 ----------

static bool test_empty_input() {
    std::printf("=== test_empty_input ===\n");
    std::vector<uint8_t> raw;

    bmv::ParseError err;
    auto r = bmv::parse_lr2(raw, &err);
    if (r.has_value()) {
        std::printf("  FAIL: should return nullopt on empty input\n");
        return false;
    }
    if (err.stage != "read" || err.reason != "empty_input") {
        std::printf("  FAIL: unexpected error (stage=%s, reason=%s)\n",
                    err.stage.c_str(), err.reason.c_str());
        return false;
    }
    std::printf("  err.stage=%s err.reason=%s\n", err.stage.c_str(), err.reason.c_str());
    std::printf("  result: %s\n\n", test_result(true));
    return true;
}

static bool test_invalid_size() {
    std::printf("=== test_invalid_size ===\n");
    // 13 字节 (非 12 倍数)
    std::vector<uint8_t> raw(13, 0);

    bmv::ParseError err;
    auto r = bmv::parse_lr2(raw, &err);
    if (r.has_value()) {
        std::printf("  FAIL: should return nullopt on invalid size\n");
        return false;
    }
    if (err.stage != "extract_events" || err.reason != "invalid_size") {
        std::printf("  FAIL: unexpected error (stage=%s, reason=%s)\n",
                    err.stage.c_str(), err.reason.c_str());
        return false;
    }
    std::printf("  err.stage=%s err.reason=%s\n", err.stage.c_str(), err.reason.c_str());
    std::printf("  err.context=%s\n", err.context.c_str());
    std::printf("  result: %s\n\n", test_result(true));
    return true;
}

static bool test_op_10_ignored() {
    std::printf("=== test_op_10_ignored ===\n");
    // op 10 = the other turntable direction → not judged in LR2
    std::vector<uint8_t> raw = {
        // record 1: time=1000, op=0 (scratch press)
        0xE8, 0x03, 0x00, 0x00,   0x00, 0x00, 0x00, 0x00,   0x01, 0x00, 0x00, 0x00,
        // record 2: time=2000, op=10 (ignored, op>=8)
        0xD0, 0x07, 0x00, 0x00,   0x0A, 0x00, 0x00, 0x00,   0x01, 0x00, 0x00, 0x00,
        // record 3: time=3000, op=1 (key 1 release)
        0xB8, 0x0B, 0x00, 0x00,   0x01, 0x00, 0x00, 0x00,   0x00, 0x00, 0x00, 0x00,
    };

    auto r = bmv::parse_lr2(raw);
    if (!r) {
        std::printf("  FAIL: parse_lr2 returned nullopt\n");
        return false;
    }

    if (r->press_events.size() != 1 || r->release_events.size() != 1) {
        std::printf("  FAIL: expected 1 press + 1 release (op=10 ignored)\n");
        std::printf("    got press=%zu release=%zu\n",
                    r->press_events.size(), r->release_events.size());
        return false;
    }
    if (r->press_events[0].lane != 0 || r->press_events[0].time_us != 1000000) {
        std::printf("  FAIL: press event mismatch (lane=%u, time=%lld)\n",
                    r->press_events[0].lane,
                    static_cast<long long>(r->press_events[0].time_us));
        return false;
    }
    if (r->release_events[0].lane != 1 || r->release_events[0].time_us != 3000000) {
        std::printf("  FAIL: release event mismatch (lane=%u, time=%lld)\n",
                    r->release_events[0].lane,
                    static_cast<long long>(r->release_events[0].time_us));
        return false;
    }

    std::printf("  result: %s\n\n", test_result(true));
    return true;
}

static bool test_synthetic_full() {
    std::printf("=== test_synthetic_full ===\n");
    // 手工构造一个最小 LR2REP:
    //   - 4 个按键事件
    //   - op 200 (seed=42)
    //   - op 103 (P1 random=Random(2))
    //   - op 210 × 2
    //   - op 200 with invalid random (op 103 with value=99) → random_option 应保持 nullopt
    std::vector<uint8_t> raw;
    auto add_record = [&](int32_t time_ms, int32_t op, int32_t value) {
        for (int s = 0; s < 4; ++s) raw.push_back(static_cast<uint8_t>((time_ms >> (s * 8)) & 0xFF));
        for (int s = 0; s < 4; ++s) raw.push_back(static_cast<uint8_t>((op     >> (s * 8)) & 0xFF));
        for (int s = 0; s < 4; ++s) raw.push_back(static_cast<uint8_t>((value  >> (s * 8)) & 0xFF));
    };

    add_record(100,  0,  1);   // scratch press @ 100ms
    add_record(200,  3,  1);   // key 3 press
    add_record(300,  3,  0);   // key 3 release
    add_record(400,  7,  0);   // key 7 release
    add_record(500, 200, 42);  // seed
    add_record(600, 103, 2);   // P1 random = Random(2)
    add_record(700, 210, 5);   // judgement
    add_record(800, 210, 4);   // judgement

    auto r = bmv::parse_lr2(raw);
    if (!r) {
        std::printf("  FAIL: parse_lr2 returned nullopt\n");
        return false;
    }

    // 4 个事件: 2 press (scratch + key3) + 2 release (key3 + key7)
    if (r->press_events.size() != 2 || r->release_events.size() != 2) {
        std::printf("  FAIL: event count mismatch (press=%zu release=%zu)\n",
                    r->press_events.size(), r->release_events.size());
        return false;
    }

    // lane 校验
    if (r->press_events[0].lane != 0 || r->press_events[0].time_us != 100000) {
        std::printf("  FAIL: scratch press wrong\n");
        return false;
    }
    if (r->press_events[1].lane != 3 || r->press_events[1].time_us != 200000) {
        std::printf("  FAIL: key 3 press wrong\n");
        return false;
    }
    if (r->release_events[0].lane != 3 || r->release_events[0].time_us != 300000) {
        std::printf("  FAIL: key 3 release wrong\n");
        return false;
    }
    if (r->release_events[1].lane != 7 || r->release_events[1].time_us != 400000) {
        std::printf("  FAIL: key 7 release wrong\n");
        return false;
    }

    // metadata
    if (!r->metadata.seed.has_value() || *r->metadata.seed != 42) {
        std::printf("  FAIL: seed wrong\n");
        return false;
    }
    if (!r->metadata.random_option.has_value() ||
        *r->metadata.random_option != bmv::RandomMode::Random) {
        std::printf("  FAIL: random_option wrong\n");
        return false;
    }

    // shuffle_pattern: Random mode + seed=42 → 应等于 compute_shuffle_pattern(42, LR2, 7)
    const auto expected = bmv::compute_shuffle_pattern(42, bmv::RandomType::LR2, 7);
    if (!pattern_eq(r->metadata.shuffle_pattern, expected)) {
        std::printf("  FAIL: shuffle_pattern mismatch\n");
        print_pattern("got     ", r->metadata.shuffle_pattern);
        print_pattern("expected", expected);
        return false;
    }

    if (!r->metadata.judgements.has_value() ||
        r->metadata.judgements->size() != 2 ||
        (*r->metadata.judgements)[0] != 5 ||
        (*r->metadata.judgements)[1] != 4) {
        std::printf("  FAIL: judgements wrong\n");
        return false;
    }

    std::printf("  result: %s\n\n", test_result(true));
    return true;
}

static bool test_invalid_random_mode() {
    std::printf("=== test_invalid_random_mode ===\n");
    // op 103 with value=99 (out of range) → random_option should stay nullopt
    std::vector<uint8_t> raw;
    auto add_record = [&](int32_t time_ms, int32_t op, int32_t value) {
        for (int s = 0; s < 4; ++s) raw.push_back(static_cast<uint8_t>((time_ms >> (s * 8)) & 0xFF));
        for (int s = 0; s < 4; ++s) raw.push_back(static_cast<uint8_t>((op     >> (s * 8)) & 0xFF));
        for (int s = 0; s < 4; ++s) raw.push_back(static_cast<uint8_t>((value  >> (s * 8)) & 0xFF));
    };

    add_record(100, 0,   1);    // scratch press
    add_record(200, 103, 99);   // invalid random mode
    add_record(300, 200, 42);   // seed

    auto r = bmv::parse_lr2(raw);
    if (!r) {
        std::printf("  FAIL: parse_lr2 returned nullopt\n");
        return false;
    }

    if (r->metadata.random_option.has_value()) {
        std::printf("  FAIL: random_option should be nullopt for invalid value 99\n");
        return false;
    }
    if (!r->metadata.seed.has_value() || *r->metadata.seed != 42) {
        std::printf("  FAIL: seed should still be 42\n");
        return false;
    }
    // shuffle_pattern should remain identity (no valid random mode)
    const std::array<int, 8> identity = {0, 1, 2, 3, 4, 5, 6, 7};
    if (!pattern_eq(r->metadata.shuffle_pattern, identity)) {
        std::printf("  FAIL: shuffle_pattern should be identity\n");
        return false;
    }

    std::printf("  result: %s\n\n", test_result(true));
    return true;
}

int main() {
    bool all_pass = true;

    all_pass &= test_normal_no_random();
    all_pass &= test_random1_seed_67();
    all_pass &= test_random2_seed_2746();
    all_pass &= test_consistency_with_existing();
    all_pass &= test_empty_input();
    all_pass &= test_invalid_size();
    all_pass &= test_op_10_ignored();
    all_pass &= test_synthetic_full();
    all_pass &= test_invalid_random_mode();

    std::printf("========================================\n");
    std::printf("%s\n", all_pass ? "所有测试通过!" : "部分测试失败!");
    std::printf("========================================\n");
    return all_pass ? 0 : 1;
}
