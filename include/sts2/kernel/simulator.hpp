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
// Complete owning copy; all handles remain local to the destination's storage.
void clone_into(State& destination, const State& source);
[[nodiscard]] bool valid_state(const State&, const Ruleset& = Ruleset::pinned()) noexcept;
} // namespace sts2::engine
