#include "sts2/engine/combat.hpp"
#include "validation.hpp"
#include "lifecycle.hpp"
#include "trace.hpp"
#include "../execution/interpreter.hpp"
#include "../execution/selection.hpp"
#include "../mechanics/cards.hpp"
#include "../mechanics/energy.hpp"
#include "../mechanics/powers.hpp"
#include "../content/monsters.hpp"
#include "../content/powers.hpp"
#include <algorithm>
#include <limits>
#include <new>
#include <stdexcept>

namespace sts2::engine {
using namespace gameplay;
namespace {
void after_enemy_turn(CombatView state, Scratch& scratch) {
    // Capture native application order before callbacks can change membership.
    for (auto* owner : {&state.player, &state.enemy}) {
        // These duration callbacks are synchronous and only erase the current
        // instance; preserve application order without allocating a snapshot.
        std::size_t i = 0;
        auto powers = owner->power_instances(state.creature_overflow);
        while (i < powers.size()) {
            const auto identity = powers[i].instance; const auto id = powers[i].definition;
            if (power_ticks_after_enemy_turn(id)) expire_power(state, scratch, *owner, identity);
            if (i < powers.size() && powers[i].instance == identity) ++i;
        }
    }
}

StepResult end_player_turn(CombatView state, Scratch& scratch, const Ruleset& rules) {
    if (!consume(scratch)) return fail(state, Restriction::execution_budget);
    discard_hand_at_turn_end(state, rules);
    state.enemy.block = 0;
    snapshot_power_turn(state, scratch, state.enemy, true);
    execute_enemy_move(state, scratch);
    if (state.player.hp == 0) end_combat(state, scratch, false);
    else {
        after_enemy_turn(state, scratch);
        snapshot_power_turn(state, scratch, state.player, false);
        state.counters.started_this_turn = state.counters.attacks_this_turn = 0;
        state.counters.zero_energy_attacks_this_turn = state.counters.non_hand_draws_this_turn = 0;
        state.player.block = 0;
        if (state.turn == std::numeric_limits<int>::max()) return fail(state, Restriction::capacity);
        for (auto it = state.cost_modifiers.begin(); it != state.cost_modifiers.end();) {
            if ((static_cast<unsigned>(it->expiry) & 2U) != 0) it = state.cost_modifiers.erase(it);
            else ++it;
        }
        ++state.turn; refresh_energy(state, 3); draw(state, scratch, 5, rules);
    }
    return result(state);
}

} // namespace
StepResult advance_combat(CombatView state, Scratch& scratch, const Ruleset& rules) noexcept try {
    if (!state.valid_storage() || !valid_powers(state) || !valid_card_extra(state)) return fail(state, Restriction::invalid_state);
    if (state.phase != CombatPhase::play || state.execution.empty()) return result(state);
    while (!state.execution.empty()) {
        if (!consume(scratch)) return fail(state, Restriction::execution_budget);
        const auto step = std::holds_alternative<std::monostate>(state.execution.back().pending) ?
            execute_instruction(state, scratch, rules) : resume_continuation(state, scratch, rules);
        if (step.status == StepStatus::engine_error || step.status == StepStatus::resource_limit || state.phase != CombatPhase::play) return step;
    }
    if (state.enemy.hp == 0 && state.phase == CombatPhase::play) end_combat(state, scratch, true);
    return result(state);
} catch (const std::bad_alloc&) { state.phase = CombatPhase::invalid; return {StepStatus::resource_limit, state.boundary, Restriction::capacity}; } catch (const std::length_error&) { state.phase = CombatPhase::invalid; return {StepStatus::resource_limit, state.boundary, Restriction::capacity}; }
catch (...) { return fail(state, Restriction::invalid_state); }
StepResult apply_combat(CombatView state, const CombatInput& input, Scratch& scratch, const Ruleset& rules) noexcept try {
    const auto restriction = validate_combat(state, input, rules);
    if (restriction != Restriction::none) return result(state, restriction);
    if (state.boundary.value == std::numeric_limits<std::uint32_t>::max()) return fail(state, Restriction::capacity);
    if (!state.clock.advance_action()) return fail(state, Restriction::clock_exhausted);
    scratch.steps = 0;
    if (state.phase == CombatPhase::card_choice) {
        const auto selected = apply_card_selection(state, input, scratch, rules);
        if (selected.status == StepStatus::engine_error || selected.status == StepStatus::resource_limit) return selected;
    } else if (input.kind == CombatInputKind::end_turn) {
        const auto ended = end_player_turn(state, scratch, rules);
        if (ended.status == StepStatus::engine_error || ended.status == StepStatus::resource_limit) return ended;
    } else if (input.kind == CombatInputKind::play_card) {
        const auto* card = state.cards.get(input.card);
        const auto cost = effective_card_cost(state, input.card, rules);
        spend_energy(state, cost);
        move_card(state, Zone::hand, Zone::play, input.card);
        if (state.counters.started_plays == UINT32_MAX) return fail(state, Restriction::capacity);
        const auto play_id = state.counters.started_plays;
        state.execution.push_back({input.card, 0, play_id, cost, state.enemy_handle});
        if (cost > 0) emit(state, scratch, CombatEventKind::energy_spent, false, false, {}, 0, cost);
        ++state.counters.started_plays; ++state.counters.started_this_turn;
        if (rules.card(card->definition)->kind == CardKind::attack) {
            ++state.counters.attacks_this_turn;
            if (cost == 0) ++state.counters.zero_energy_attacks_this_turn;
        }
        emit(state, scratch, CombatEventKind::play_started, false, false, input.card, play_id);
        const auto advanced = advance_combat(state, scratch, rules);
        if (advanced.status == StepStatus::engine_error || advanced.status == StepStatus::resource_limit) return advanced;
    } else {
        discard_card(state, scratch, input.card);
        state.phase = CombatPhase::play;
        const auto advanced = advance_combat(state, scratch, rules);
        if (advanced.status == StepStatus::engine_error || advanced.status == StepStatus::resource_limit) return advanced;
    }
    if (state.phase == CombatPhase::play && !state.execution.empty()) {
        const auto advanced = advance_combat(state, scratch, rules);
        if (advanced.status == StepStatus::engine_error || advanced.status == StepStatus::resource_limit) return advanced;
    }
    if (state.phase == CombatPhase::invalid) return {StepStatus::engine_error, state.boundary, Restriction::execution_budget};
    ++state.boundary.value;
    emit(state, scratch, CombatEventKind::combat_updated);
    return result(state);
} catch (const std::bad_alloc&) {
    state.phase = CombatPhase::invalid;
    return {StepStatus::resource_limit, state.boundary, Restriction::capacity};
} catch (const std::length_error&) {
    state.phase = CombatPhase::invalid;
    return {StepStatus::resource_limit, state.boundary, Restriction::capacity};
} catch (...) { return fail(state, Restriction::invalid_state); }
StepResult apply_power_scenario(CombatView state, const PowerApplication& operation, Scratch& scratch) noexcept {
    if (!valid_combat(state)) return result(state, Restriction::invalid_state);
    if (state.phase != CombatPhase::play || !state.execution.empty()) return result(state, Restriction::wrong_phase);
    if (operation.definition >= PowerId::count || operation.amount < -999999999 || operation.amount > 999999999 ||
        (operation.source.present() && !state.cards.get(operation.source))) return result(state, Restriction::malformed_action);
    try {
        (void)apply_power(state, scratch, operation.definition, operation.amount, operation.enemy_owner, false,
            operation.source, operation.has_applier, operation.applier_enemy);
        return result(state);
    } catch (...) { return fail(state, Restriction::capacity); }
}
}
