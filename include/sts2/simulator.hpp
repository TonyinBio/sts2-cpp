#pragma once
#include "sts2/action.hpp"
#include "sts2/state.hpp"

namespace sts2 {
[[nodiscard]] LegalActions legal_actions(const CombatState& state) noexcept;
// Invalid actions return false and leave state unchanged.
[[nodiscard]] bool step(CombatState& state, Action action) noexcept;
} // namespace sts2
