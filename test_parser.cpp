// 单元测试：验证 unified/parser
//   - parse_replay 工厂函数：按扩展名分派
//   - .brd 新版 (keyinput) → parse_brd_new
//   - .brd 旧版 (keylog)   → parse_brd_old
//   - .lr2rep              → parse_lr2
//   - 不支持的扩展名 / 文件不存在 → 返回 nullopt 并填充 ParseError
//
// 用例：
//   1. testfiles/sp/..._088_Normal.brd        (new BRD)
//   2. testfiles/086/..._1.brd                (old BRD)
//   3. testfiles/sp/eeff3a9a7cd557a05308ba1296752db2.lr2rep
//   4. unsupported.brd                        (虚构扩展名)
//   5. nonexistent_file.brd                   (文件不存在)

#include "replay/unified/parser.h"
#include "replay/unified/brd_parser.h"
#include "replay/unified/lr2_parser.h"
#include "replay/unified/random.h"

#include <cstdio>
#include <cstdlib>
#include <string>

namespace {

const char* test_result(bool ok) { return ok ? "PASS" : "FAIL"; }

void print_err(const bmv::ParseError& err) {
    std::printf("  err.file_path=%s\n", err.file_path.c_str());
    std::printf("  err.stage    =%s\n", err.stage.c_str());
    std::printf("  err.reason   =%s\n", err.reason.c_str());
    std::printf("  err.context  =%s\n", err.context.c_str());
}

bool pattern_eq(const std::array<int, 8>& a, const std::array<int, 8>& b) {
    for (int i = 0; i < 8; ++i) if (a[i] != b[i]) return false;
    return true;
}

} // namespace

// ---------- 1. .brd 新版 (keyinput) ----------

static bool test_brd_new_dispatch() {
    std::printf("=== test_brd_new_dispatch ===\n");
    const std::string path = "testfiles/sp/"
        "bc1462814a47c6a0e2d6a6779014f78e8f6f94c3b49ab6ecb9a5752d18137e16_088_Normal.brd";

    auto r = bmv::parse_replay(path);
    if (!r) {
        std::printf("  FAIL: parse_replay returned nullopt\n");
        return false;
    }

    const size_t total = r->press_events.size() + r->release_events.size();
    std::printf("  press=%zu  release=%zu  total=%zu  duration_us=%lld\n",
                r->press_events.size(), r->release_events.size(), total,
                static_cast<long long>(r->metadata.duration_us));

    if (total != 4512) {
        std::printf("  FAIL: expected 4512 frames, got %zu\n", total);
        return false;
    }
    if (r->press_events.empty() || r->release_events.empty()) {
        std::printf("  FAIL: missing press or release events\n");
        return false;
    }

    const std::array<int, 8> identity = {0, 1, 2, 3, 4, 5, 6, 7};
    if (!pattern_eq(r->metadata.shuffle_pattern, identity)) {
        std::printf("  FAIL: expected identity shuffle (no pattern in file)\n");
        return false;
    }

    if (!r->metadata.seed.has_value() || *r->metadata.seed != 14266788) {
        std::printf("  FAIL: expected seed=14266788\n");
        return false;
    }

    std::printf("  result: %s\n\n", test_result(true));
    return true;
}

// ---------- 2. .brd 旧版 (keylog) ----------

static bool test_brd_old_dispatch() {
    std::printf("=== test_brd_old_dispatch ===\n");
    const std::string path = "testfiles/086/"
        "086_random_bc1462814a47c6a0e2d6a6779014f78e8f6f94c3b49ab6ecb9a5752d18137e16_1.brd";

    auto r = bmv::parse_replay(path);
    if (!r) {
        std::printf("  FAIL: parse_replay returned nullopt\n");
        return false;
    }

    const size_t total = r->press_events.size() + r->release_events.size();
    std::printf("  press=%zu  release=%zu  total=%zu  duration_us=%lld\n",
                r->press_events.size(), r->release_events.size(), total,
                static_cast<long long>(r->metadata.duration_us));

    if (total != 3734) {
        std::printf("  FAIL: expected 3734 valid entries, got %zu\n", total);
        return false;
    }

    const std::array<int, 8> expected = {0, 3, 7, 6, 1, 2, 5, 4};
    if (!pattern_eq(r->metadata.shuffle_pattern, expected)) {
        std::printf("  FAIL: shuffle_pattern mismatch\n");
        return false;
    }

    if (!r->metadata.random_option.has_value() ||
        *r->metadata.random_option != bmv::RandomMode::Random) {
        std::printf("  FAIL: expected random_option=Random(2)\n");
        return false;
    }

    if (!r->metadata.seed.has_value() || *r->metadata.seed != 11470162) {
        std::printf("  FAIL: expected seed=11470162\n");
        return false;
    }

    std::printf("  result: %s\n\n", test_result(true));
    return true;
}

// ---------- 3. .lr2rep ----------

static bool test_lr2rep_dispatch() {
    std::printf("=== test_lr2rep_dispatch ===\n");
    const std::string path = "testfiles/sp/eeff3a9a7cd557a05308ba1296752db2.lr2rep";

    auto r = bmv::parse_replay(path);
    if (!r) {
        std::printf("  FAIL: parse_replay returned nullopt\n");
        return false;
    }

    std::printf("  press=%zu  release=%zu  duration_us=%lld\n",
                r->press_events.size(), r->release_events.size(),
                static_cast<long long>(r->metadata.duration_us));

    if (r->press_events.size() != 2272 || r->release_events.size() != 2273) {
        std::printf("  FAIL: event count mismatch\n");
        return false;
    }
    if (r->metadata.duration_us != 143137000) {
        std::printf("  FAIL: duration_us mismatch (got %lld, want 143137000)\n",
                    static_cast<long long>(r->metadata.duration_us));
        return false;
    }

    const std::array<int, 8> identity = {0, 1, 2, 3, 4, 5, 6, 7};
    if (!pattern_eq(r->metadata.shuffle_pattern, identity)) {
        std::printf("  FAIL: expected identity shuffle for off mode\n");
        return false;
    }

    std::printf("  result: %s\n\n", test_result(true));
    return true;
}

// ---------- 4. 不支持的扩展名 ----------

static bool test_unsupported_extension() {
    std::printf("=== test_unsupported_extension ===\n");
    const std::string path = "testfiles/sp/anata_g24.bme";

    bmv::ParseError err;
    auto r = bmv::parse_replay(path, &err);
    if (r.has_value()) {
        std::printf("  FAIL: should return nullopt for .bme\n");
        return false;
    }
    if (err.stage != "extension") {
        std::printf("  FAIL: stage should be 'extension', got '%s'\n",
                    err.stage.c_str());
        print_err(err);
        return false;
    }
    if (err.reason != "unsupported_extension") {
        std::printf("  FAIL: reason should be 'unsupported_extension', got '%s'\n",
                    err.reason.c_str());
        print_err(err);
        return false;
    }
    if (err.file_path != path) {
        std::printf("  FAIL: file_path mismatch (got '%s', want '%s')\n",
                    err.file_path.c_str(), path.c_str());
        return false;
    }
    if (err.context.find(".bme") == std::string::npos) {
        std::printf("  FAIL: context should mention '.bme'\n");
        print_err(err);
        return false;
    }
    print_err(err);
    std::printf("  result: %s\n\n", test_result(true));
    return true;
}

// ---------- 5. 文件不存在 ----------

static bool test_missing_file() {
    std::printf("=== test_missing_file ===\n");
    const std::string path = "testfiles/__definitely_does_not_exist__.brd";

    bmv::ParseError err;
    auto r = bmv::parse_replay(path, &err);
    if (r.has_value()) {
        std::printf("  FAIL: should return nullopt for missing file\n");
        return false;
    }
    if (err.stage != "read") {
        std::printf("  FAIL: stage should be 'read', got '%s'\n",
                    err.stage.c_str());
        print_err(err);
        return false;
    }
    if (err.reason != "file_open_failed") {
        std::printf("  FAIL: reason should be 'file_open_failed', got '%s'\n",
                    err.reason.c_str());
        print_err(err);
        return false;
    }
    if (err.file_path != path) {
        std::printf("  FAIL: file_path mismatch (got '%s', want '%s')\n",
                    err.file_path.c_str(), path.c_str());
        return false;
    }
    print_err(err);
    std::printf("  result: %s\n\n", test_result(true));
    return true;
}

// ---------- 6. 错误处理：未提供 error 指针时也应安全运行 ----------

static bool test_null_error_pointer() {
    std::printf("=== test_null_error_pointer ===\n");
    const std::string path = "testfiles/sp/anata_g24.bme";

    auto r = bmv::parse_replay(path, nullptr);
    if (r.has_value()) {
        std::printf("  FAIL: should return nullopt for .bme\n");
        return false;
    }
    std::printf("  result: %s\n\n", test_result(true));
    return true;
}

// ---------- 7. 错误处理：LR2 文件不存在 ----------

static bool test_missing_lr2rep() {
    std::printf("=== test_missing_lr2rep ===\n");
    const std::string path = "testfiles/__no_such_lr2.lr2rep";

    bmv::ParseError err;
    auto r = bmv::parse_replay(path, &err);
    if (r.has_value()) {
        std::printf("  FAIL: should return nullopt for missing lr2rep\n");
        return false;
    }
    if (err.stage != "read") {
        std::printf("  FAIL: stage should be 'read', got '%s'\n",
                    err.stage.c_str());
        print_err(err);
        return false;
    }
    print_err(err);
    std::printf("  result: %s\n\n", test_result(true));
    return true;
}

int main() {
    bool all_pass = true;

    all_pass &= test_brd_new_dispatch();
    all_pass &= test_brd_old_dispatch();
    all_pass &= test_lr2rep_dispatch();
    all_pass &= test_unsupported_extension();
    all_pass &= test_missing_file();
    all_pass &= test_null_error_pointer();
    all_pass &= test_missing_lr2rep();

    std::printf("========================================\n");
    std::printf("%s\n", all_pass ? "所有测试通过!" : "部分测试失败!");
    std::printf("========================================\n");
    return all_pass ? 0 : 1;
}
