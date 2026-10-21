#pragma once
#include "sts2/state/run.hpp"

namespace sts2::engine {
// Default A0 normal-combat rewards with the supported starter relic set. All
// random positions and odds are committed together after successful generation.
[[nodiscard]] StepResult generate_combat_rewards(SetupState&, CombatRewards&, std::uint8_t act_index);
} // namespace sts2::engine
