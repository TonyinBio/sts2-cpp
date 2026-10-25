#pragma once
#include "sts2/state/run.hpp"
#include "sts2/kernel/simulator.hpp"

namespace sts2::engine {
enum class RunInputKind : std::uint8_t { choose_option, proceed, travel, abandon, take_reward, skip_rewards, choose_reward_card, reward_alternative, discard_potion };
struct RunInput {
    BoundaryId expected_boundary;
    RunInputKind kind;
    std::uint32_t option = 0;
    std::int32_t col = 0, row = 0;
};
[[nodiscard]] bool valid_run(const RunState&) noexcept;
[[nodiscard]] StepResult initialize_run(RunState&, const SeededConfig&);
[[nodiscard]] StepResult initialize_seeded_combat(RunState&, const SeededConfig&);
// Declared scenario operation: enter a supported native room without changing
// generated encounter queues or map topology. Gameplay still uses apply_player.
[[nodiscard]] StepResult enter_combat_scenario(RunState&, BoundaryId, std::string_view encounter);
[[nodiscard]] StepResult apply_combat(RunState&, const Input&, Scratch&);
[[nodiscard]] StepResult apply_run(RunState&, const RunInput&);
} // namespace sts2::engine
