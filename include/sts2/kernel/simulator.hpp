#pragma once
#include "sts2/state/state.hpp"

namespace sts2::engine {
enum class InputKind : std::uint8_t { play_card, end_turn, select_card };
struct Input {
    BoundaryId expected_boundary;
    InputKind kind;
    CardHandle card{};
    bool target_enemy = false;
};
class Simulator {
public:
    explicit Simulator(const Ruleset& rules = Ruleset::pinned(), Limits limits = {}) noexcept
        : rules_(rules), limits_(limits) {}
    [[nodiscard]] StepResult initialize(State&, const CombatConfig&, Scratch&) const;
    [[nodiscard]] StepResult advance(State&, Scratch&) const noexcept;
    [[nodiscard]] StepResult apply(State&, const Input&, Scratch&) const noexcept;
    [[nodiscard]] Restriction validate(const State&, const Input&) const noexcept;
private:
    const Ruleset& rules_;
    Limits limits_;
};
// Diagnostic scenario command, separate from legal player inputs. Uses the
// same native power application semantics, preserving source/applier identity.
struct PowerScenario {
    PowerId definition;
    std::int32_t amount;
    bool enemy_owner = false;
    bool applier_enemy = false;
    bool has_applier = true;
    CardHandle source;
};
// Declared consumer perturbation probe: remove a later card/power in ordinary
// pass, then apply real Strength between ordinary and late native captures.
[[nodiscard]] StepResult begin_card_played_probe(State&, BoundaryId, std::uint32_t play, CardHandle remove_card, std::uint32_t remove_power) noexcept;
[[nodiscard]] StepResult step_card_played_probe(State&, BoundaryId) noexcept;
[[nodiscard]] StepResult capture_duration_scenario(State&, BoundaryId) noexcept;
[[nodiscard]] StepResult step_duration_scenario(State&, BoundaryId) noexcept;
[[nodiscard]] StepResult remove_power_scenario(State&, BoundaryId, std::uint32_t index) noexcept;
[[nodiscard]] StepResult apply_power_scenario(State&, BoundaryId, const PowerScenario&) noexcept;
// Complete owning copy; all handles remain local to the destination's storage.
void clone_into(State& destination, const State& source);
[[nodiscard]] bool valid_state(const State&, const Ruleset& = Ruleset::pinned()) noexcept;
} // namespace sts2::engine
