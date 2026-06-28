#pragma once
#include <cstdint>

namespace bmv {

class LR2Random {
public:
    explicit LR2Random(uint32_t seed);
    uint32_t next();
    uint32_t nextInt(uint32_t n);

private:
    uint32_t mt[624]   = {};
    uint32_t mtr[624]  = {};
    int      mti       = 0;
    void     generateMT();
    void     temperAll();
};

void build_random_lane_pattern(int seed, int L, int display_to_bms[8], int bms_to_display[8]);

} // namespace bmv
