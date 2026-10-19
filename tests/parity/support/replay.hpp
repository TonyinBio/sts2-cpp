#pragma once
#include "state_recording.hpp"
#include <functional>

namespace sts2::parity {
// The backend owns initialization, persistent action identities and settled-boundary
// export. Only the initial reference state is supplied for restoration; subsequent
// expected checkpoints are never passed to the backend. Unsupported actions throw.
struct ReplayBackend {
    std::function<void(const nlohmann::json& setup, const Snapshot& initial)> initialize;
    std::function<void(const nlohmann::json& action)> apply;
    std::function<Snapshot()> snapshot;
};
struct ReplayFailure {
    std::string checkpoint; // "initial" or the action ID
    std::vector<Difference> differences;
};
// Stops at the first divergent checkpoint or replay error. Sparse checkpoints
// still execute every intervening action. Throws on invalid/unassociated input.
// reference must match the expected provenance exactly, supplied independently.
std::vector<ReplayFailure> replay_parity(
    std::string_view action_bytes, std::string_view state_bytes,
    const std::set<std::string>& supported_features,
    const nlohmann::json& reference, const ReplayBackend& backend);
}
