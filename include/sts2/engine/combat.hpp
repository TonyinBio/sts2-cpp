#pragma once
#include "sts2/state/combat.hpp"

namespace sts2::engine {
enum class CombatInputKind : std::uint8_t { play_card, end_turn, select_card };
struct CombatInput {
    BoundaryId expected_boundary;
    CombatInputKind kind;
    CardHandle card{};
    bool target_enemy = false;
};
[[nodiscard]] StepResult initialize_combat(CombatView, const CombatConfig&, Scratch&,
    const Ruleset& = Ruleset::pinned(), Limits = {});
[[nodiscard]] StepResult advance_combat(CombatView, Scratch&,
    const Ruleset& = Ruleset::pinned()) noexcept;
[[nodiscard]] StepResult apply_combat(CombatView, const CombatInput&, Scratch&,
    const Ruleset& = Ruleset::pinned()) noexcept;
[[nodiscard]] Restriction validate_combat(ConstCombatView, const CombatInput&,
    const Ruleset& = Ruleset::pinned()) noexcept;
// Complete owning copy; all handles remain local to the destination's storage.
[[nodiscard]] bool valid_combat(ConstCombatView, const Ruleset& = Ruleset::pinned()) noexcept;
} // namespace sts2::engine
