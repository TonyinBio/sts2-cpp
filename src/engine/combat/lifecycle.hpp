#pragma once
#include "sts2/state/combat.hpp"

namespace sts2::engine::gameplay {
StepResult result(ConstCombatView, Restriction = Restriction::none) noexcept;
StepResult fail(CombatView, Restriction) noexcept;
bool consume(Scratch&) noexcept;
void end_combat(CombatView, Scratch&, bool won);
}
