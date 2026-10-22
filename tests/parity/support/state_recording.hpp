#pragma once
#include "recording.hpp"
#include "snapshot.hpp"
#include <functional>

namespace sts2::parity {
// Validates the envelope, every run-parity snapshot, and correspondence with action input.
// Checkpoints may cover any strictly ordered subset of action IDs.
// The caller must also verify actions_sha256 against the exact source file bytes.
// A supplied validator may reuse successful snapshot checks by content; envelope
// and action/checkpoint correspondence are always checked independently.
void validate_state_recording(const nlohmann::json& value, const ActionRecording& actions,
                              const std::set<std::string>& supported_features,
                              const std::function<void(const nlohmann::json&)>& validate_snapshot = {});
}
