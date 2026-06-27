#include "java_random.h"
#include <vector>

namespace bmv {

static constexpr int64_t MULTIPLIER = 0x5DEECE66DLL;
static constexpr int64_t ADDEND     = 0xBLL;
static constexpr int64_t MASK       = 0xFFFFFFFFFFFFLL;

JavaRandom::JavaRandom(int64_t initialSeed) {
    seed = (initialSeed ^ MULTIPLIER) & MASK;
}

int32_t JavaRandom::next(int bits) {
    seed = (seed * MULTIPLIER + ADDEND) & MASK;
    return static_cast<int32_t>(static_cast<uint64_t>(seed) >> (48 - bits));
}

int32_t JavaRandom::nextInt(int32_t n) {
    if ((n & -n) == n) {
        return static_cast<int32_t>(
            (static_cast<int64_t>(n) * next(31)) >> 31);
    }
    int64_t bits, val;
    do {
        bits = static_cast<int64_t>(next(31)) & 0x7FFFFFFFLL;
        val = bits % n;
    } while (bits - val + (n - 1) < 0);
    return static_cast<int32_t>(val);
}

void build_brd_random_pattern(int32_t random_option, int64_t seed, int shuffle_pattern[8]) {
    JavaRandom rng(seed);

    std::vector<int> keys;
    if (random_option == 2) {
        keys = {0, 1, 2, 3, 4, 5, 6};
    } else if (random_option == 9) {
        keys = {0, 1, 2, 3, 4, 5, 6, 7};
    } else {
        for (int i = 0; i < 8; ++i)
            shuffle_pattern[i] = i;
        return;
    }

    int result[8] = {0, 1, 2, 3, 4, 5, 6, 7};
    std::vector<int> available = keys;

    for (size_t lane = 0; lane < keys.size(); ++lane) {
        int32_t r = rng.nextInt(static_cast<int32_t>(available.size()));
        result[keys[lane]] = available[r];
        available.erase(available.begin() + r);
    }

    // 直接用 result 数组转换坐标，不需要反转
    // result[i] 表示 beatoraja display position i 显示的 BMS lane
    for (int display_bev = 0; display_bev < 8; ++display_bev) {
        int bms_bev = result[display_bev];
        int display_our = (display_bev == 7) ? 0 : display_bev + 1;
        int bms_our = (bms_bev == 7) ? 0 : bms_bev + 1;
        shuffle_pattern[display_our] = bms_our;
    }
}

} // namespace bmv
