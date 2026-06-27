// 单元测试：验证 unified/random 统一接口
#include "replay/unified/random.h"
#include <cstdio>
#include <array>

static bool test_brd(int64_t seed, int random_option, const std::array<int, 8>& expected) {
    auto got = bmv::compute_shuffle_pattern(static_cast<int>(seed), bmv::RandomType::OldBRD, random_option);
    std::printf("[OldBRD] seed=%lld, random_option=%d\n", seed, random_option);
    std::printf("  got     : %d %d %d %d %d %d %d %d\n",
                got[0], got[1], got[2], got[3], got[4], got[5], got[6], got[7]);
    std::printf("  expected: %d %d %d %d %d %d %d %d\n",
                expected[0], expected[1], expected[2], expected[3],
                expected[4], expected[5], expected[6], expected[7]);
    bool pass = (got == expected);
    std::printf("  result: %s\n\n", pass ? "PASS" : "FAIL");
    return pass;
}

static bool test_lr2(int seed, int L, const std::array<int, 8>& expected) {
    auto got = bmv::compute_shuffle_pattern(seed, bmv::RandomType::LR2, L);
    std::printf("[LR2] seed=%d, L=%d\n", seed, L);
    std::printf("  got     : %d %d %d %d %d %d %d %d\n",
                got[0], got[1], got[2], got[3], got[4], got[5], got[6], got[7]);
    std::printf("  expected: %d %d %d %d %d %d %d %d\n",
                expected[0], expected[1], expected[2], expected[3],
                expected[4], expected[5], expected[6], expected[7]);
    bool pass = (got == expected);
    std::printf("  result: %s\n\n", pass ? "PASS" : "FAIL");
    return pass;
}

int main() {
    bool all_pass = true;

    // OldBRD 测试用例
    all_pass &= test_brd(2871559, 2, {0, 6, 5, 3, 7, 1, 2, 4});
    all_pass &= test_brd(1432039, 2, {0, 5, 6, 7, 4, 2, 3, 1});

    // LR2 测试用例
    all_pass &= test_lr2(24332, 7, {7, 1, 2, 0, 6, 3, 5, 4});

    std::printf(all_pass ? "所有测试通过!\n" : "部分测试失败!\n");
    return all_pass ? 0 : 1;
}
