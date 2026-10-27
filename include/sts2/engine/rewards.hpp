#pragma once
#include "sts2/state/run.hpp"

namespace sts2::engine {
// Default A0 normal-combat rewards with the supported starter relic set.
// Commit offers, reward RNG and odds together only after successful generation.
[[nodiscard]] StepResult generate_combat_rewards(RunView);
} // namespace sts2::engine
