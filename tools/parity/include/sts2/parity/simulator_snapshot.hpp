#pragma once
#include "sts2/parity/replay.hpp"
#include "sts2/state.hpp"

namespace sts2::parity {
// Honest bridge to the current scaffold. Partial diagnostics are never a parity
// snapshot: snapshot() fails until the simulator can produce the whole contract.
class SimulatorSnapshot {
public:
    void initialize(const nlohmann::json& setup);
    // Explicit fixture initialization for unit tests, never used by backend().
    void load(const Snapshot& fixture);
    [[nodiscard]] nlohmann::json produced_state() const;
    [[nodiscard]] std::vector<std::string> unsupported_fields() const;
    [[nodiscard]] Snapshot snapshot() const;
    void apply(const nlohmann::json& action);
    [[nodiscard]] ReplayBackend backend();
    const CombatState& state() const noexcept { return state_; }
private:
    bool initialized_ = false;
    CombatState state_;
    std::string player_ = "/players/0", creature_ = "/combat/creatures/player_0", enemy_ = "/combat/creatures/enemy_0";
    std::vector<std::string> hand_, discarded_;
    std::map<std::string, std::string> card_ids_;
};
}
