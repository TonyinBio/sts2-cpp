#include "gameplay.hpp"
#include "sts2/core/sort.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace sts2::engine::gameplay {
StepResult result(ConstCombatView state, Restriction reason) noexcept {
    if (reason != Restriction::none) return {StepStatus::invalid_action, state.boundary, reason};
    switch (state.phase) {
    case CombatPhase::combat_won: return {StepStatus::combat_won, state.boundary};
    case CombatPhase::player_died: return {StepStatus::player_died, state.boundary};
    case CombatPhase::uninitialized: case CombatPhase::invalid: return {StepStatus::engine_error, state.boundary, Restriction::invalid_state};
    default: return {StepStatus::awaiting_input, state.boundary};
    }
}
StepResult fail(CombatView state, Restriction reason) noexcept {
    state.phase = CombatPhase::invalid;
    return {StepStatus::engine_error, state.boundary, reason};
}
bool consume(Scratch& scratch) noexcept { return ++scratch.steps <= scratch.step_limit; }
void end_combat(CombatView state, Scratch& scratch, bool won) {
    state.phase = won ? CombatPhase::combat_won : CombatPhase::player_died;
    state.execution.clear(); state.cost_modifiers.clear(); clear_selection(state);
    if (won) {
        state.player.clear_powers(); state.enemy.clear_powers();
        for (auto& zone : state.zone_views()) zone.clear();
    } else {
        for (auto id : state.player.power_ids()) {
            auto& power = state.player.power(id);
            power.active = false; emit(state, scratch, CombatEventKind::power_changed, false, false, {}, power.instance);
        }
        state.player.clear_powers();
    }
    emit(state, scratch, won ? CombatEventKind::combat_won : CombatEventKind::combat_lost);
    state.player.block = 0;
    if (won && state.burning_blood) {
        (void)heal(state.player, 6);
    }
    if (won) {
        // Terminal combat has no suspended work or visible piles. Preserve all
        // scalars/identity progress, but release its completed collection graph.
        CombatState terminal(state.overflow.allocator());
        std::memcpy(&terminal.data, &state.data, sizeof(CombatData));
        terminal.player = state.player; terminal.enemy = state.enemy; terminal.boundary = state.boundary; terminal.clock = state.clock; terminal.rng = state.rng;
        terminal.data.for_each_sequence([](auto& sequence) { sequence = std::remove_reference_t<decltype(sequence)>{}; });
        terminal.view().seed = std::string_view(state.seed);
        state.replace(std::move(terminal));
    }
}
}
