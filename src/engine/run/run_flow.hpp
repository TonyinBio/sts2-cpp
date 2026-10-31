#pragma once
#include "sts2/engine/run.hpp"

namespace sts2::engine::flow {
enum class ValidationMode { visible, execution };
struct RunActionPlan {
    std::uint32_t entering_generation = 0, next_card_instance = 0;
    bool generate_rewards = false, reward_kind = false;
    MapNodeId destination = no_node;
    std::size_t potion_slot = 0;
};
std::uint32_t highest_combat_generation(ConstRunView) noexcept;
std::uint32_t next_combat_generation(ConstRunView) noexcept;
bool supported_combat_inventory(ConstRunView);
// Prepare combat without publishing run changes or advancing the player clock.
StepResult prepare_combat(CombatView, ConstRunView, Scratch&, BoundaryId rejection_boundary, std::int32_t enemy_hp = 0);
bool valid_room_context(ConstRunView) noexcept;
void apply_reward_input(RunView, const RunInput&, const RunActionPlan&);
void apply_room_option(RunView, const RunInput&);
void child_combat_finished(RunView, bool won) noexcept;
bool return_from_room(RunView) noexcept;
// Pure planning distinguishes visible legality from hidden entry capability.
StepResult plan_run_input(ConstRunView, const RunInput&, RunActionPlan&, ValidationMode);
}
