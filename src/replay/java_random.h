#pragma once
#include <cstdint>

namespace bmv {

class JavaRandom {
public:
    explicit JavaRandom(int64_t initialSeed);
    int32_t next(int bits);
    int32_t nextInt(int32_t n);

private:
    int64_t seed;
};

void build_brd_random_pattern(int32_t random_option, int64_t seed, int shuffle_pattern[8]);

} // namespace bmv
