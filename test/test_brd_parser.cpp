// 单元测试：验证 unified/brd_parser
//   - parse_brd_new: 新版 BRD (keyinput, base64 + gzip)
//   - parse_brd_old: 旧版 BRD (keylog, JSON 数组)
//
// 用例：
//   1. testfiles/sp/..._088_Normal.brd        (new BRD, 4,512 frames)
//   2. testfiles/086/..._1.brd                (old BRD, 4,498 entries,
//                                              seed=11470162, opt=2)
//   3. testfiles/086/..._2.brd                (old BRD, 4,519 entries,
//                                              seed=5035094,  opt=2)
//
// 验证项：
//   - 解析成功
//   - 事件数 / lane / 时间戳正确
//   - shuffle_pattern 正确还原
//   - 错误情况返回结构化 ParseError

#include "replay/unified/brd_parser.h"
#include "replay/unified/random.h"
#include "replay/gzip.h"
#include "replay/base64.h"

#include <json.hpp>

#include <array>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

namespace {

// ---------- 辅助函数 ----------

// 读取二进制文件到 vector
std::vector<uint8_t> read_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return {};
    const size_t size = static_cast<size_t>(f.tellg());
    f.seekg(0, std::ios::beg);
    std::vector<uint8_t> data(size);
    f.read(reinterpret_cast<char*>(data.data()), size);
    return data;
}

// 读取并 GZIP-解压 BRD 顶层 JSON 文件，返回 JSON 对象
std::optional<nlohmann::json> load_brd_json(const std::string& path) {
    auto raw = read_file(path);
    if (raw.empty()) return std::nullopt;

    auto decompressed = bmv::gzip::decompress(raw.data(), raw.size());
    if (decompressed.empty()) return std::nullopt;

    std::string text(decompressed.begin(), decompressed.end());
    auto j = nlohmann::json::parse(text, nullptr, false);
    if (j.is_discarded()) return std::nullopt;
    return j;
}

// 打印 shuffle_pattern
void print_pattern(const char* tag, const std::array<int, 8>& p) {
    std::printf("  %s: %d %d %d %d %d %d %d %d\n", tag,
                p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7]);
}

bool pattern_eq(const std::array<int, 8>& a, const std::array<int, 8>& b) {
    for (int i = 0; i < 8; ++i) if (a[i] != b[i]) return false;
    return true;
}

const char* test_result(bool ok) { return ok ? "PASS" : "FAIL"; }

} // namespace

// ---------- 测试用例 ----------

static bool test_new_brd_basic() {
    std::printf("=== test_new_brd_basic ===\n");
    const std::string path = "testfiles/sp/"
        "bc1462814a47c6a0e2d6a6779014f78e8f6f94c3b49ab6ecb9a5752d18137e16_088_Normal.brd";

    auto j_opt = load_brd_json(path);
    if (!j_opt) {
        std::printf("  FAIL: cannot load %s\n", path.c_str());
        return false;
    }

    auto r = bmv::parse_brd_new(*j_opt);
    if (!r) {
        std::printf("  FAIL: parse_brd_new returned nullopt\n");
        return false;
    }

    // 1) 事件总数 = keyinput 解码后的 9 字节帧数 (预计算 = 4512 帧)
    const size_t total = r->press_events.size() + r->release_events.size();
    std::printf("  press=%zu  release=%zu  total=%zu  duration_us=%lld\n",
                r->press_events.size(), r->release_events.size(), total,
                static_cast<long long>(r->metadata.duration_us));

    if (total != 4512) {
        std::printf("  FAIL: expected 4512 frames, got %zu\n", total);
        return false;
    }

    // 2) 至少应有一些 press / release 事件
    if (r->press_events.empty() || r->release_events.empty()) {
        std::printf("  FAIL: missing press or release events\n");
        return false;
    }

    // 3) 所有事件 lane 都在 [0,7] 范围内
    auto check_lane = [](const std::vector<bmv::ReplayEvent>& evs, const char* kind) {
        for (const auto& e : evs) {
            if (e.lane > 7) {
                std::printf("  FAIL: %s event has invalid lane %u\n", kind, e.lane);
                return false;
            }
        }
        return true;
    };
    if (!check_lane(r->press_events, "press")) return false;
    if (!check_lane(r->release_events, "release")) return false;

    // 4) 该文件 laneShufflePattern=[null]，shuffle_pattern 应保持恒等
    const std::array<int, 8> identity = {0, 1, 2, 3, 4, 5, 6, 7};
    if (!pattern_eq(r->metadata.shuffle_pattern, identity)) {
        std::printf("  FAIL: expected identity shuffle (no pattern in file)\n");
        print_pattern("got     ", r->metadata.shuffle_pattern);
        print_pattern("expected", identity);
        return false;
    }

    // 5) seed 应被提取 (randomoptionseed = 14266788)
    if (!r->metadata.seed.has_value() || *r->metadata.seed != 14266788) {
        std::printf("  FAIL: expected seed=14266788\n");
        return false;
    }

    // 6) random_option 应为空（未指定）
    if (r->metadata.random_option.has_value()) {
        std::printf("  FAIL: random_option should be empty for new BRD\n");
        return false;
    }

    std::printf("  result: %s\n\n", test_result(true));
    return true;
}

static bool test_old_brd_seed_11470162() {
    std::printf("=== test_old_brd_seed_11470162 ===\n");
    const std::string path = "testfiles/086/"
        "086_random_bc1462814a47c6a0e2d6a6779014f78e8f6f94c3b49ab6ecb9a5752d18137e16_1.brd";

    auto j_opt = load_brd_json(path);
    if (!j_opt) {
        std::printf("  FAIL: cannot load %s\n", path.c_str());
        return false;
    }

    auto r = bmv::parse_brd_old(*j_opt);
    if (!r) {
        std::printf("  FAIL: parse_brd_old returned nullopt\n");
        return false;
    }

    // 1) 事件总数 = 4498 条目中有效条目 = 3734（764 个缺少 keycode 字段被过滤）
    const size_t total = r->press_events.size() + r->release_events.size();
    std::printf("  press=%zu  release=%zu  total=%zu  duration_us=%lld\n",
                r->press_events.size(), r->release_events.size(), total,
                static_cast<long long>(r->metadata.duration_us));

    if (total != 3734) {
        std::printf("  FAIL: expected 3734 valid entries, got %zu\n", total);
        return false;
    }

    // 2) shuffle_pattern 严格匹配 {0,3,7,6,1,2,5,4}
    const std::array<int, 8> expected = {0, 3, 7, 6, 1, 2, 5, 4};
    if (!pattern_eq(r->metadata.shuffle_pattern, expected)) {
        std::printf("  FAIL: shuffle_pattern mismatch\n");
        print_pattern("got     ", r->metadata.shuffle_pattern);
        print_pattern("expected", expected);
        return false;
    }

    // 3) random_option = 2
    if (!r->metadata.random_option.has_value() ||
        *r->metadata.random_option != bmv::RandomMode::Random) {
        std::printf("  FAIL: expected random_option=Random(2)\n");
        return false;
    }

    // 4) seed = 11470162
    if (!r->metadata.seed.has_value() || *r->metadata.seed != 11470162) {
        std::printf("  FAIL: expected seed=11470162\n");
        return false;
    }

    std::printf("  result: %s\n\n", test_result(true));
    return true;
}

static bool test_old_brd_seed_5035094() {
    std::printf("=== test_old_brd_seed_5035094 ===\n");
    const std::string path = "testfiles/086/"
        "086_randombc1462814a47c6a0e2d6a6779014f78e8f6f94c3b49ab6ecb9a5752d18137e16_2.brd";

    auto j_opt = load_brd_json(path);
    if (!j_opt) {
        std::printf("  FAIL: cannot load %s\n", path.c_str());
        return false;
    }

    auto r = bmv::parse_brd_old(*j_opt);
    if (!r) {
        std::printf("  FAIL: parse_brd_old returned nullopt\n");
        return false;
    }

    const size_t total = r->press_events.size() + r->release_events.size();
    std::printf("  press=%zu  release=%zu  total=%zu  duration_us=%lld\n",
                r->press_events.size(), r->release_events.size(), total,
                static_cast<long long>(r->metadata.duration_us));

    // 4519 条目中有效条目 = 3723（796 个缺少 keycode 字段被过滤）
    if (total != 3723) {
        std::printf("  FAIL: expected 3723 valid entries, got %zu\n", total);
        return false;
    }

    // shuffle_pattern 用 compute_shuffle_pattern 独立验证
    const auto expected =
        bmv::compute_shuffle_pattern(5035094, bmv::RandomType::OldBRD, 2);
    if (!pattern_eq(r->metadata.shuffle_pattern, expected)) {
        std::printf("  FAIL: shuffle_pattern mismatch\n");
        print_pattern("got     ", r->metadata.shuffle_pattern);
        print_pattern("expected", expected);
        return false;
    }

    if (!r->metadata.seed.has_value() || *r->metadata.seed != 5035094) {
        std::printf("  FAIL: expected seed=5035094\n");
        return false;
    }

    std::printf("  result: %s\n\n", test_result(true));
    return true;
}

// ---------- 错误情况 ----------

static bool test_new_brd_missing_keyinput() {
    std::printf("=== test_new_brd_missing_keyinput ===\n");
    nlohmann::json j = nlohmann::json::object();
    j["some_other_field"] = "hello";

    bmv::ParseError err;
    auto r = bmv::parse_brd_new(j, &err);
    if (r.has_value()) {
        std::printf("  FAIL: should return nullopt on missing keyinput\n");
        return false;
    }
    if (err.reason != "missing_field") {
        std::printf("  FAIL: reason should be 'missing_field', got '%s'\n",
                    err.reason.c_str());
        return false;
    }
    if (err.stage != "extract_events") {
        std::printf("  FAIL: stage should be 'extract_events', got '%s'\n",
                    err.stage.c_str());
        return false;
    }
    std::printf("  err.stage=%s err.reason=%s\n", err.stage.c_str(), err.reason.c_str());
    std::printf("  err.context=%s\n", err.context.c_str());
    std::printf("  result: %s\n\n", test_result(true));
    return true;
}

static bool test_new_brd_corrupt_base64() {
    std::printf("=== test_new_brd_corrupt_base64 ===\n");
    nlohmann::json j = nlohmann::json::object();
    // Empty base64 string decodes to empty -> should fail
    j["keyinput"] = "";

    bmv::ParseError err;
    auto r = bmv::parse_brd_new(j, &err);
    if (r.has_value()) {
        std::printf("  FAIL: should return nullopt on empty base64\n");
        return false;
    }
    if (err.reason != "base64_decode_failed" && err.reason != "gzip_decompress_failed") {
        std::printf("  FAIL: unexpected reason '%s'\n", err.reason.c_str());
        return false;
    }
    std::printf("  err.stage=%s err.reason=%s\n", err.stage.c_str(), err.reason.c_str());
    std::printf("  err.context=%s\n", err.context.c_str());
    std::printf("  result: %s\n\n", test_result(true));
    return true;
}

static bool test_new_brd_corrupt_gzip() {
    std::printf("=== test_new_brd_corrupt_gzip ===\n");
    nlohmann::json j = nlohmann::json::object();
    // "AAAA" base64 -> 0x00 0x00 0x00, not valid gzip
    j["keyinput"] = "AAAA";

    bmv::ParseError err;
    auto r = bmv::parse_brd_new(j, &err);
    if (r.has_value()) {
        std::printf("  FAIL: should return nullopt on invalid gzip\n");
        return false;
    }
    if (err.reason != "gzip_decompress_failed" && err.reason != "base64_decode_failed") {
        std::printf("  FAIL: unexpected reason '%s'\n", err.reason.c_str());
        return false;
    }
    std::printf("  err.stage=%s err.reason=%s\n", err.stage.c_str(), err.reason.c_str());
    std::printf("  err.context=%s\n", err.context.c_str());
    std::printf("  result: %s\n\n", test_result(true));
    return true;
}

static bool test_old_brd_missing_keylog() {
    std::printf("=== test_old_brd_missing_keylog ===\n");
    nlohmann::json j = nlohmann::json::object();
    j["randomoption"]     = 2;
    j["randomoptionseed"] = 1234;

    bmv::ParseError err;
    auto r = bmv::parse_brd_old(j, &err);
    if (r.has_value()) {
        std::printf("  FAIL: should return nullopt on missing keylog\n");
        return false;
    }
    if (err.reason != "missing_field") {
        std::printf("  FAIL: reason should be 'missing_field', got '%s'\n",
                    err.reason.c_str());
        return false;
    }
    std::printf("  err.stage=%s err.reason=%s\n", err.stage.c_str(), err.reason.c_str());
    std::printf("  err.context=%s\n", err.context.c_str());
    std::printf("  result: %s\n\n", test_result(true));
    return true;
}

static bool test_old_brd_optional_random() {
    std::printf("=== test_old_brd_optional_random ===\n");
    // random_option 非 2/9 → shuffle_pattern 应保持恒等
    nlohmann::json j = nlohmann::json::object();
    j["randomoption"]     = 0; // Off
    j["randomoptionseed"] = 9999;
    j["keylog"]           = nlohmann::json::array({
        {{"presstime", 1000000}, {"keycode", 0}, {"pressed", true}},
        {{"presstime", 2000000}, {"keycode", 7}, {"pressed", false}}
    });

    auto r = bmv::parse_brd_old(j);
    if (!r) {
        std::printf("  FAIL: should succeed for valid keylog with opt=0\n");
        return false;
    }

    const std::array<int, 8> identity = {0, 1, 2, 3, 4, 5, 6, 7};
    if (!pattern_eq(r->metadata.shuffle_pattern, identity)) {
        std::printf("  FAIL: shuffle_pattern should be identity for opt=0\n");
        print_pattern("got     ", r->metadata.shuffle_pattern);
        return false;
    }
    if (r->metadata.random_option != bmv::RandomMode::Off) {
        std::printf("  FAIL: random_option should be Off\n");
        return false;
    }
    if (r->press_events.size() != 1 || r->release_events.size() != 1) {
        std::printf("  FAIL: event count mismatch (press=%zu, release=%zu)\n",
                    r->press_events.size(), r->release_events.size());
        return false;
    }
    // keycode 0 -> lane 1, keycode 7 -> lane 0
    if (r->press_events[0].lane != 1 || r->release_events[0].lane != 0) {
        std::printf("  FAIL: lane mapping wrong (press lane=%u, release lane=%u)\n",
                    r->press_events[0].lane, r->release_events[0].lane);
        return false;
    }
    if (r->press_events[0].time_us != 1000000 || r->release_events[0].time_us != 2000000) {
        std::printf("  FAIL: timestamp mismatch\n");
        return false;
    }
    std::printf("  result: %s\n\n", test_result(true));
    return true;
}

int main() {
    bool all_pass = true;

    all_pass &= test_new_brd_basic();
    all_pass &= test_old_brd_seed_11470162();
    all_pass &= test_old_brd_seed_5035094();

    all_pass &= test_new_brd_missing_keyinput();
    all_pass &= test_new_brd_corrupt_base64();
    all_pass &= test_new_brd_corrupt_gzip();
    all_pass &= test_old_brd_missing_keylog();
    all_pass &= test_old_brd_optional_random();

    std::printf("========================================\n");
    std::printf("%s\n", all_pass ? "所有测试通过!" : "部分测试失败!");
    std::printf("========================================\n");
    return all_pass ? 0 : 1;
}
