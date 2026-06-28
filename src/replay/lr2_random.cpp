#include "lr2_random.h"
#include <cstdint>

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

} // namespace bmv
