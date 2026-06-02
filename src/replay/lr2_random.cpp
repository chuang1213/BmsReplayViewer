#include "lr2_random.h"
#include <cassert>
#include <cstdint>
#include <cstdio>

namespace bmv {

static constexpr uint32_t MATRIX_A   = 0x9908B0DFu;
static constexpr uint32_t UPPER_MASK = 0x80000000u;
static constexpr uint32_t LOWER_MASK = 0x7FFFFFFFu;

LR2Random::LR2Random(uint32_t seed) {
    uint32_t s = seed;
    for (int i = 0; i < 624; ++i) {
        mt[i] = (s & 0xFFFF0000u);
        s = 69069u * s + 1u;
        mt[i] = (mt[i] | ((s & 0xFFFF0000u) >> 16));
        s = 69069u * s + 1u;
    }
    generateMT();
    temperAll();
}

void LR2Random::generateMT() {
    static const uint32_t mag01[2] = {0u, MATRIX_A};
    uint32_t y;

    for (int kk = 0; kk < 624 - 397; ++kk) {
        y = (mt[kk] & UPPER_MASK) | (mt[kk + 1] & LOWER_MASK);
        mt[kk] = mt[kk + 397] ^ (y >> 1) ^ mag01[y & 1u];
    }
    for (int kk = 624 - 397; kk < 624 - 1; ++kk) {
        y = (mt[kk] & UPPER_MASK) | (mt[kk + 1] & LOWER_MASK);
        mt[kk] = mt[kk + (397 - 624)] ^ (y >> 1) ^ mag01[y & 1u];
    }
    y = (mt[623] & UPPER_MASK) | (mt[0] & LOWER_MASK);
    mt[623] = mt[396] ^ (y >> 1) ^ mag01[y & 1u];
}

void LR2Random::temperAll() {
    for (int kk = 0; kk < 624; ++kk) {
        uint32_t y = mt[kk];
        y ^= (y >> 11);
        y ^= (y << 7) & 0x9D2C5680u;
        y ^= (y << 15) & 0xEFC60000u;
        y ^= (y >> 18);
        mtr[kk] = y;
    }
}

uint32_t LR2Random::next() {
    if (mti >= 624) {
        generateMT();
        temperAll();
        mti = 0;
    }
    return mtr[mti++];
}

uint32_t LR2Random::nextInt(uint32_t n) {
    return static_cast<uint32_t>(
        (static_cast<uint64_t>(next()) * n) >> 32);
}

void build_random_lane_pattern(int seed, int L, int display_to_bms[8], int bms_to_display[8]) {
    LR2Random rng(static_cast<uint32_t>(seed));

    int targets[8] = {0, 1, 2, 3, 4, 5, 6, 7};
    for (int a = 1; a < L; ++a) {
        int b = a + static_cast<int>(rng.nextInt(static_cast<uint32_t>(L - a + 1)));
        int t = targets[a];
        targets[a] = targets[b];
        targets[b] = t;
    }

    // targets maps BMS lane → display position
    for (int i = 0; i < 8; ++i)
        bms_to_display[i] = targets[i];

    // display_to_bms = inverse of bms_to_display
    display_to_bms[0] = 0;
    for (int i = 1; i <= L; ++i)
        display_to_bms[bms_to_display[i]] = i;
}

void test_lr2_random() {
    int d2b[8];
    int b2d[8];
    build_random_lane_pattern(24332, 7, d2b, b2d);

    const int expected_d2b[8] = {0, 1, 7, 5, 2, 4, 3, 6};
    for (int i = 0; i < 8; ++i)
        assert(d2b[i] == expected_d2b[i]);

    const int expected_b2d[8] = {0, 1, 4, 6, 5, 3, 7, 2};
    for (int i = 0; i < 8; ++i)
        assert(b2d[i] == expected_b2d[i]);

    std::fprintf(stdout, "[LR2Random] Seed=24332 d2b: %d %d %d %d %d %d %d %d  (PASS)\n",
                 d2b[0], d2b[1], d2b[2], d2b[3], d2b[4], d2b[5], d2b[6], d2b[7]);
    std::fprintf(stdout, "[LR2Random] Seed=24332 b2d: %d %d %d %d %d %d %d %d  (PASS)\n",
                 b2d[0], b2d[1], b2d[2], b2d[3], b2d[4], b2d[5], b2d[6], b2d[7]);
}

} // namespace bmv
