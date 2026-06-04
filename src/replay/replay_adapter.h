#pragma once
#include "raw_input_event.h"
#include "replay_data.h"
#include "core/time_map.h"

namespace bmv {

// ┌─────────────────────────────────────────────────┐
// │ TEMPORARY ADAPTER (Phase 3 deletion candidate)   │
// │ Converts unified ReplayInput → legacy ReplayData │
// │ for compatibility during transition. Once all     │
// │ downstream consumers are refactored to use        │
// │ RawInputEvent directly, this function is deleted. │
// └─────────────────────────────────────────────────┘
ReplayData replay_input_to_replay_data(const ReplayInput& input,
                                        const TimeMap& time_map);

} // namespace bmv
