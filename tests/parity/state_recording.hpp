#pragma once
#include "recording.hpp"
#include "snapshot.hpp"

namespace sts2::parity {
// Validates the envelope, every combat snapshot, and correspondence with action input.
// The caller must also verify actions_sha256 against the exact source file bytes.
void validate_state_recording(const nlohmann::json& value, const ActionRecording& actions,
                              const std::set<std::string>& supported_features);
}
