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

// beatoraja 7KEY windows (ms) — verified against JudgeProperty.java:32
//   Base (judgerank=HARD@100%): PG=±20  GR=±60  GD=±150  BD_fast=280  BD_slow=220  POOR=500
//   Rank multipliers from JudgeWindowRule.NORMAL: {25,50,75,100,125} for {V_EASY,EASY,NORMAL,HARD,V_HARD}
//   BMS #RANK 0=V_HARD→125%, 1=HARD→100%, 2=NORMAL→75%, 3=EASY→50%
static const JudgeWindow kBeatorajaWindows[4] = {
    { 25,  75, 188, 275, 500, 263, 275 },  // VERY_HARD (125%)
    { 20,  60, 150, 220, 500, 210, 220 },  // HARD (100%)
    { 15,  45, 113, 165, 500, 158, 165 },  // NORMAL (75%)
    { 10,  30,  75, 110, 500, 105, 110 },  // EASY (50%)
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
