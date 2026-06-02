#pragma once
#include <cstdint>

namespace bmv {

enum class JudgeSystem { LR2, Beatoraja };
enum class JudgeRank : uint8_t { VERY_HARD = 0, HARD, NORMAL, EASY };

struct JudgeWindow {
    int pg;
    int gr;
    int gd;
    int bd;       // symmetric BAD (LR2) or max of fast/slow (BEATORAJA)
    int poor;
    int bd_fast;  // maximum early offset for BAD (ms), stored as positive
    int bd_slow;  // maximum late offset for BAD (ms), stored as positive
};

class JudgeProfile {
public:
    static const JudgeWindow& get_window(JudgeSystem sys, JudgeRank rank);
    static const char*       rank_string(JudgeRank rank);
};

} // namespace bmv
