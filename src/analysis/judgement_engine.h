#pragma once
#include "core/timeline.h"
#include "replay/replay.h"
#include <vector>
#include <cstdint>

namespace bmv {

enum class JudgeSystem {
    LR2,
    Beatoraja
};

enum class Judge {
    PGREAT,
    GREAT,
    GOOD,
    BAD,
    POOR
};

struct HitResult {
    const NoteEvent* note = nullptr;
    const ReplayHit* hit  = nullptr;

    Judge  judge = Judge::POOR;
    int    offset_ms = 0;   // positive = SLOW, negative = FAST
    bool   fast = false;
    bool   slow = false;
    bool   is_release = false;
};

struct AccuracyStatistics {
    int pgreat = 0;
    int great  = 0;
    int good   = 0;
    int bad    = 0;
    int poor   = 0;

    int fast = 0;
    int slow = 0;

    double mean_offset   = 0.0;
    double stddev_offset = 0.0;
};

class JudgementEngine {
public:
    void set_system(JudgeSystem sys) { system_ = sys; }
    JudgeSystem system() const { return system_; }

    void set_player_index(int idx) { player_idx_ = idx; }
    int  player_index() const { return player_idx_; }

    void compute_lane_mappings(const ReplayData& replay);

    void analyze(const Timeline& timeline,
                 const ReplayData& replay,
                 int rank);

    const std::vector<HitResult>& results() const { return results_; }
    const std::vector<const NoteEvent*>& missed_notes() const { return missed_notes_; }
    const HitResult* result_for_hit(size_t hit_index) const;
    const AccuracyStatistics& statistics() const { return stats_; }
    const int* display_to_bms() const { return computed_display_to_bms_; }
    const int* bms_to_display() const { return computed_bms_to_display_; }

private:
    JudgeSystem system_ = JudgeSystem::Beatoraja;
    int         player_idx_ = 0;

    std::vector<HitResult> results_;
    std::vector<const NoteEvent*> missed_notes_;
    AccuracyStatistics     stats_;
    int computed_display_to_bms_[8] = {0, 1, 2, 3, 4, 5, 6, 7};
    int computed_bms_to_display_[8] = {0, 1, 2, 3, 4, 5, 6, 7};

    Judge classify_lr2(int offset_ms, int rank) const;
    Judge classify_beatoraja(int offset_ms, int rank) const;
};

} // namespace bmv
