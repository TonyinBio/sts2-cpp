#pragma once
#include "replay.hpp"
#include "sts2/state.hpp"
#include <optional>

namespace sts2::parity {
// Incremental test adapter, not a full run-state implementation. Unmodeled fields
// are frozen from the INITIAL snapshot only, including RNG, history and AI. Never
// update this baseline from expected checkpoints: their changes must cause diffs.
class SimulatorSnapshot {
public:
    void load(const Snapshot& initial);
    [[nodiscard]] Snapshot snapshot() const;
    void apply(const nlohmann::json& action);
    // Callbacks borrow this object; it must outlive the replay.
    [[nodiscard]] ReplayBackend backend();
    const CombatState& state() const noexcept { return state_; }
private:
    std::optional<Snapshot> initial_;
    CombatState state_;
    std::string player_, creature_, enemy_;
    std::vector<std::string> hand_, discarded_;
    std::map<std::string, std::string> card_ids_;
};
}
