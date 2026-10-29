#pragma once
#include "sts2/state/combat.hpp"

namespace sts2::engine {
[[nodiscard]] std::int32_t effective_card_cost(ConstCombatView, CardHandle, const Ruleset& = Ruleset::pinned()) noexcept;
enum class CombatInputKind : std::uint8_t { play_card, end_turn, select_card, deselect_card, confirm_selection };
struct CombatInput {
    BoundaryId expected_boundary;
    CombatInputKind kind;
    CardHandle card{};
    bool target_enemy = false;
    PromptId prompt {};
};
[[nodiscard]] StepResult initialize_combat(CombatView, const CombatConfig&, Scratch&,
    const Ruleset& = Ruleset::pinned(), Limits = {});
[[nodiscard]] StepResult advance_combat(CombatView, Scratch&,
    const Ruleset& = Ruleset::pinned()) noexcept;
[[nodiscard]] StepResult apply_combat(CombatView, const CombatInput&, Scratch&,
    const Ruleset& = Ruleset::pinned()) noexcept;
[[nodiscard]] Restriction validate_combat(ConstCombatView, const CombatInput&,
    const Ruleset& = Ruleset::pinned()) noexcept;
// Diagnostic operation corresponding to native PowerCmd; no player clock step.
struct PowerApplication {
    PowerId definition;
    std::int32_t amount;
    bool enemy_owner = false, has_applier = true, applier_enemy = false;
    CardHandle source {};
};
[[nodiscard]] StepResult apply_power_scenario(CombatView, const PowerApplication&, Scratch&) noexcept;
// Complete owning copy; all handles remain local to the destination's storage.
[[nodiscard]] bool valid_combat(ConstCombatView, const Ruleset& = Ruleset::pinned()) noexcept;
} // namespace sts2::engine
