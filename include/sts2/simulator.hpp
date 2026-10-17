#pragma once
#include "sts2/action.hpp"
#include "sts2/state.hpp"

namespace sts2 {
[[nodiscard]] constexpr int card_cost(Card card) noexcept { return card == Card::bash ? 2 : 1; }
// Numeric seed initializes a standalone combat, not the game's full-run seed recipe.
// Reference parity imports the initial snapshot and all RNG streams instead.
[[nodiscard]] CombatState make_starter_combat(std::uint64_t seed = 0) noexcept;
[[nodiscard]] LegalActions legal_actions(const CombatState& state) noexcept;
// Invalid actions return false and leave state unchanged. Input state must be valid
// for this profile; the JSON adapter validates before constructing it.
[[nodiscard]] bool step(CombatState& state, Action action) noexcept;
} // namespace sts2
