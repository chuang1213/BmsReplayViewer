#include "judge_profile.h"

namespace bmv {

// LR2 timing windows (ms) — verified against OpenLR2 LR2_bmsload.cpp:3697-3736
//   Rank 0 (VERY_HARD): PG=8  GR=24  GD=40
//   Rank 1 (HARD):      PG=15 GR=30  GD=60
//   Rank 2 (NORMAL):    PG=18 GR=40  GD=100
//   Rank 3 (EASY):      PG=21 GR=60  GD=120
//   BAD=200, POOR=1000 for all ranks
static const JudgeWindow kLr2Windows[4] = {
    { 12,  24,  60, 200, 1000, 200, 200 },  // VERY_HARD  (pg:8→12, gd:40→60 per lr2rep_format.md §3.4)
    { 15,  30,  80, 200, 1000, 200, 200 },  // HARD       (gd:60→80 per lr2rep_format.md §3.4)
    { 18,  40, 100, 200, 1000, 200, 200 },  // NORMAL
    { 21,  60, 120, 200, 1000, 200, 200 },  // EASY
};

// beatoraja 7K SEVENKEYS windows (ms) — vanilla beatoraja, from
//   beatoraja-src play/JudgeProperty.java (SEVENKEYS):
//     base (µs): PG ±20  GR ±60  GD ±150  BD [late 280 / early 220]  MS [late 150 / early 500]
//   Per-#RANK scaling = JudgeWindowRule.NORMAL.judgerank {25,50,75,100,125}% for
//   #RANK {0=VERYHARD, 1=HARD, 2=NORMAL, 3=EASY, 4=VERYEASY}; MS (poor) is NOT scaled.
//   NOTE: beatoraja judges on dmtime∈[lo,hi] with ASYMMETRIC BD/MS. Here PG/GR/GD are
//   symmetric (exact); bd_fast/bd_slow hold the early/late BAD bounds, bd = late bound.
//   ⚠ The previous table had the rank scaling INVERTED (VERYHARD must be tightest, not widest).
//                                pg  gr   gd   bd  poor  bd_fast(early) bd_slow(late)
static const JudgeWindow kBeatorajaWindows[4] = {
    {  5,  15,  37,  70, 500,  55,  70 },  // VERY_HARD  #RANK 0  (25%)
    { 10,  30,  75, 140, 500, 110, 140 },  // HARD       #RANK 1  (50%)
    { 15,  45, 112, 210, 500, 165, 210 },  // NORMAL     #RANK 2  (75%)
    { 20,  60, 150, 280, 500, 220, 280 },  // EASY       #RANK 3  (100%)
};

const JudgeWindow& JudgeProfile::get_window(JudgeSystem sys, JudgeRank rank) {
    int idx = static_cast<int>(rank);
    if (idx < 0) idx = 0; if (idx > 3) idx = 3;
    return (sys == JudgeSystem::LR2) ? kLr2Windows[idx] : kBeatorajaWindows[idx];
}

const char* JudgeProfile::rank_string(JudgeRank rank) {
    switch (rank) {
        case JudgeRank::VERY_HARD: return "VERY HARD";
        case JudgeRank::HARD:      return "HARD";
        case JudgeRank::NORMAL:    return "NORMAL";
        case JudgeRank::EASY:      return "EASY";
    }
    return "???";
}

} // namespace bmv
