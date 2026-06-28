#pragma once
#include "types.h"
#include "replay/replay_data.h"
#include "core/time_map.h"

namespace bmv {

// ┌─────────────────────────────────────────────────────────────┐
// │ TEMPORARY ADAPTER (Phase 3 deletion candidate)             │
// │ Converts unified UnifiedReplay → legacy ReplayData          │
// │ for compatibility during transition. Once all downstream     │
// │ consumers are refactored to use UnifiedReplay directly,      │
// │ this function is deleted.                                   │
// └─────────────────────────────────────────────────────────────┘
//
// BRD path: merges press_events + release_events into a time-ordered
// stream (press before release on equal timestamp) and runs a state
// machine to pair each press with its matching release. EOF-active
// keys fall back to the last observed tick and increment `unmatched`.
//
// LR2 path: each press_event / release_event is emitted as its own
// ReplayHit (tick_start == tick_end). LR2 metadata is translated to
// the legacy P1 slot.
ReplayData unified_to_replay_data(const UnifiedReplay& replay,
                                   const TimeMap& time_map,
                                   ReplayFormat format);

} // namespace bmv
