#pragma once
#include "sts2/state/run.hpp"
#include "sts2/engine/combat.hpp"
#include <variant>

namespace sts2::engine {
enum class RunInputKind : std::uint8_t { choose_option, proceed, travel, abandon, take_reward, skip_rewards, choose_reward_card, reward_alternative, discard_potion };
struct RunInput {
    BoundaryId expected_boundary;
    RunInputKind kind;
    std::uint32_t option = 0;
    std::int32_t col = 0, row = 0;
};
using PlayerInput = std::variant<RunInput, CombatInput>;

[[nodiscard]] bool valid_run(const RunState&) noexcept;
[[nodiscard]] StepResult initialize_run(RunState&, const SeededConfig&);
[[nodiscard]] StepResult initialize_seeded_combat(RunState&, const SeededConfig&);
// Declared scenario operation: enter a supported native room without changing
// generated encounter queues or map topology. Gameplay still uses apply_action.
[[nodiscard]] StepResult enter_combat_scenario(RunState&, BoundaryId, std::string_view encounter);
// Execute one transition, retaining explicit historical proceed boundaries.
[[nodiscard]] StepResult apply_input(RunState&, const PlayerInput&, Scratch&);
} // namespace sts2::engine
