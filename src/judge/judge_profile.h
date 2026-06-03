#pragma once
#include <cstdint>

namespace bmv {

enum class JudgeSystem { LR2, Beatoraja };
enum class JudgeRank : uint8_t { VERY_HARD = 0, HARD, NORMAL, EASY, VERY_EASY };

struct JudgeWindow {
    int pg;
    int gr;
    int gd;
    int bd;       // symmetric BAD (LR2) or max of fast/slow (BEATORAJA)
    int poor;
    int bd_fast;  // maximum early offset for BAD (ms), stored as positive
    int bd_slow;  // maximum late offset for BAD (ms), stored as positive
};

// beatoraja SEVENKEYS asymmetric judge windows (µs): 5 tiers PG/GR/GD/BD/MS, each
// {lo,hi}. dmtime = note_us − press_us (>0 = early); tier j matches if dmtime ∈ [lo,hi];
// the MS tier doubles as POOR. Unlike LR2 these are NOT symmetric.
struct BeatorajaWindow {
    long long lo_us;
    long long hi_us;
};

class JudgeProfile {
public:
    static const JudgeWindow& get_window(JudgeSystem sys, JudgeRank rank);
    static const char*       rank_string(JudgeRank rank);
    // rank: 0..4 (#RANK VERYHARD..VERYEASY); scratch=true for the turntable lane.
    static void beatoraja_windows(int rank, bool scratch, BeatorajaWindow out[5]);
};

} // namespace bmv
