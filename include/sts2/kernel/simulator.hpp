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
    [[nodiscard]] StepResult initialize(StateView, const CombatConfig&, Scratch&) const;
    [[nodiscard]] StepResult advance(StateView, Scratch&) const noexcept;
    [[nodiscard]] StepResult apply(StateView, const Input&, Scratch&) const noexcept;
    [[nodiscard]] Restriction validate(ConstStateView, const Input&) const noexcept;
private:
    const Ruleset& rules_;
    Limits limits_;
};
// Complete owning copy; all handles remain local to the destination's storage.
[[nodiscard]] bool valid_state(ConstStateView, const Ruleset& = Ruleset::pinned()) noexcept;
} // namespace sts2::engine
