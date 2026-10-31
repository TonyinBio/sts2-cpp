#pragma once
#include "sts2/state/combat.hpp"

namespace sts2::engine::gameplay {
bool valid_continuations(ConstCombatView, const Ruleset&) noexcept;
StepResult resume_continuation(CombatView, Scratch&, const Ruleset&);
StepResult execute_instruction(CombatView, Scratch&, const Ruleset&);
}
