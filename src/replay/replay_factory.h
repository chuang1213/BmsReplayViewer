#pragma once
#include "raw_input_event.h"
#include <string>
#include <optional>

namespace bmv {

// Auto-detect replay format by file extension and parse into unified ReplayInput.
// Returns std::nullopt on failure (unsupported format / parse error).
std::optional<ReplayInput> parse_replay(const std::string& path);

} // namespace bmv
