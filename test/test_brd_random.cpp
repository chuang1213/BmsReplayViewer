// 单元测试：验证 BRD 随机算法
// 测试用例1：seed=2871559, random_option=2
// 预期输出：[0, 6, 5, 3, 7, 1, 2, 4]（统一格式，0=scratch, 1-7=keys）
// 测试用例2：seed=1432039, random_option=2
// 预期输出：[0, 5, 6, 7, 4, 2, 3, 1]

#include "replay/java_random.h"
#include <cstdio>
#include <array>

bool test_case(int64_t seed, int random_option, int expected[8]) {
    int shuffle_pattern[8];
    
    bmv::build_brd_random_pattern(random_option, seed, shuffle_pattern);
    
    std::printf("seed=%lld, random_option=%d\n", seed, random_option);
    std::printf("输出: ");
    for (int i = 0; i < 8; ++i) {
        std::printf("%d ", shuffle_pattern[i]);
    }
    std::printf("\n");
    
    std::printf("预期: ");
    for (int i = 0; i < 8; ++i) {
        std::printf("%d ", expected[i]);
    }
    std::printf("\n");
    
    bool pass = true;
    for (int i = 0; i < 8; ++i) {
        if (shuffle_pattern[i] != expected[i]) {
            pass = false;
            break;
        }
    }
    
    if (pass) {
        std::printf("结果: PASS\n\n");
        return true;
    } else {
        std::printf("结果: FAIL\n\n");
        return false;
    }
}

int main() {
    bool all_pass = true;
    
    // 测试用例1
    int expected1[8] = {0, 6, 5, 3, 7, 1, 2, 4};
    all_pass &= test_case(2871559, 2, expected1);
    
    // 测试用例2
    int expected2[8] = {0, 5, 6, 7, 4, 2, 3, 1};
    all_pass &= test_case(1432039, 2, expected2);
    
    if (all_pass) {
        std::printf("所有测试通过!\n");
        return 0;
    } else {
        std::printf("部分测试失败!\n");
        return 1;
    }
}
