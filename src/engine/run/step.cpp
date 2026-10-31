#include "sts2/engine/run.hpp"
#include "sts2/engine/rewards.hpp"
#include "run_flow.hpp"
#include <algorithm>
#include <limits>
#include <new>

namespace sts2::engine {
using namespace flow;
StepResult enter_combat_scenario(RunState& state, BoundaryId boundary, std::string_view encounter, CombatTrace trace) {
    if (!valid_run(state)) return {StepStatus::engine_error, state.data.boundary, Restriction::invalid_state};
    auto state_view = state.view();
    if (boundary != state_view.boundary) return {StepStatus::invalid_action, state_view.boundary, Restriction::stale_boundary};
    if (state_view.phase != RunPhase::map || state_view.combat_generation == 0 || (state_view.rewards && !state_view.rewards->resolved))
        return {StepStatus::invalid_action, state_view.boundary, Restriction::wrong_phase};
    if (encounter != "ENCOUNTER.SHRINKER_BEETLE_WEAK" || state_view.ascension != 0 ||
        SetupRules::name(state_view.character) != "CHARACTER.IRONCLAD")
        return {StepStatus::unsupported_content, state_view.boundary, Restriction::unsupported_setup};
    if (!supported_combat_inventory(state_view))
        return {StepStatus::unsupported_content, state_view.boundary, Restriction::unsupported_setup};
    if (next_combat_generation(state_view) == 0 || state_view.acts[state_view.act_index].normal_visited >= state_view.acts[state_view.act_index].normal.size()) return {StepStatus::resource_limit, state_view.boundary, Restriction::capacity};
    try {
        RunState next = state; auto next_view = next.view();
        CombatState combat;
        Scratch scratch; scratch.trace = trace;
        const auto result = prepare_combat(combat, next_view, scratch, state_view.boundary);
        if (result.status != StepStatus::awaiting_input) return result;
        next_view.rewards.reset();
        next_view.combat_generation = next_combat_generation(next_view);
        const auto run_clock = next_view.clock; const auto run_boundary = next_view.boundary;
        next_view.combat = std::move(combat);
        // Scenario entry is not a player action and does not consume the clock.
        next_view.clock = run_clock; next_view.boundary = run_boundary;
        trace.emit(*next_view.combat, {CombatEventKind::combat_updated});
        ++next_view.acts[next_view.act_index].normal_visited;
        next_view.phase = RunPhase::combat; next_view.direct_combat = false;
        state = std::move(next);
        return {StepStatus::awaiting_input, state_view.boundary};
    } catch (const std::bad_alloc&) { return {StepStatus::resource_limit, state_view.boundary, Restriction::capacity}; } catch (const std::length_error&) { return {StepStatus::resource_limit, state_view.boundary, Restriction::capacity}; }
}
namespace {
StepResult apply_combat_transition(RunState& state, const CombatInput& input, Scratch& scratch) {
    auto state_view = state.view();
    if (state_view.phase != RunPhase::combat || !state_view.combat)
        return {StepStatus::invalid_action, state_view.boundary, Restriction::wrong_phase};
    const auto result = apply_combat(*state_view.combat, input, scratch);
    if (result.status == StepStatus::invalid_action || result.status == StepStatus::unsupported_content) return result;
    if (result.status == StepStatus::combat_won) state_view.phase = RunPhase::combat_done;
    else if (result.status == StepStatus::player_died) state_view.phase = RunPhase::complete;
    else if (result.status == StepStatus::engine_error || result.status == StepStatus::resource_limit) state_view.phase = RunPhase::invalid;
    if (!state_view.direct_combat && (result.status == StepStatus::combat_won || result.status == StepStatus::player_died)) {
        child_combat_finished(state_view, result.status == StepStatus::combat_won);
        state_view.combat_generation = highest_combat_generation(state_view);
        state_view.combat.reset();
        auto* player = state_view.creatures.get(state_view.data.player);
        const auto hp = player->hp, max_hp = player->max_hp;
        player->clear(); player->hp = hp; player->max_hp = max_hp;
    }
    return result;
}
StepResult apply_run_transition(RunState& state, const RunInput& input, Scratch& scratch) {
    auto state_view = state.view();
    RunActionPlan plan;
    const auto checked = plan_run_input(state_view, input, plan, ValidationMode::execution);
    if (checked.status != StepStatus::awaiting_input) return checked;
    const auto entering_generation = plan.entering_generation;
    const auto generate_rewards = plan.generate_rewards;
    const auto destination = plan.destination;
    const auto reward_kind = plan.reward_kind;
    std::optional<CombatState> entering_combat;
    if (destination != no_node) {
        try {
            entering_combat.emplace();
            const auto initialized = prepare_combat(*entering_combat, state_view, scratch, state_view.boundary);
            if (initialized.status != StepStatus::awaiting_input) return initialized;
        } catch (const std::bad_alloc&) { return {StepStatus::resource_limit, state_view.boundary, Restriction::capacity}; }
          catch (const std::length_error&) { return {StepStatus::resource_limit, state_view.boundary, Restriction::capacity}; }
    }
    if (state_view.boundary.value == UINT32_MAX) { state_view.phase = RunPhase::invalid; return {StepStatus::resource_limit, state_view.boundary, Restriction::capacity}; }
    try {
        // All validation precedes mutation. Prepare owning growth before time/payment.
        if (input.kind == RunInputKind::choose_option && state_view.phase == RunPhase::neow) state_view.relics.reserve(state_view.relics.size() + 1);
        if (entering_combat) {
            state_view.visited.reserve(state_view.visited.size() + 1);
        }
        if (input.kind == RunInputKind::choose_reward_card) state_view.deck.reserve(state_view.deck.size() + 1);
        if (input.kind == RunInputKind::take_reward) state_view.potions.reserve(static_cast<std::size_t>(state_view.potion_slots));
        // Preflight time before reward generation mutates run RNG/odds. Generation
        // itself stages all fallible work locally and commits only on success.
        auto clock = state_view.clock;
        if (!clock.advance_action()) { state_view.phase = RunPhase::invalid; return {StepStatus::engine_error, state_view.boundary, Restriction::clock_exhausted}; }
        if (generate_rewards) {
            const auto generated = generate_combat_rewards(state_view);
            if (generated.status != StepStatus::awaiting_input)
                return {generated.status, state_view.boundary, generated.reason};
        }
        state_view.clock = clock;
        if (reward_kind) {
            apply_reward_input(state_view, input, plan);
        } else if (input.kind == RunInputKind::discard_potion) {
            state_view.potions[input.option] = no_content;
        } else if (input.kind == RunInputKind::choose_option) {
            apply_room_option(state_view, input);
        } else if (generate_rewards) {
            state_view.phase = RunPhase::rewards;
        } else if (entering_combat) {
            state_view.rewards.reset();
            state_view.combat_generation = entering_generation;
            state_view.current = destination; state_view.visited.push_back(destination);
            ++state_view.acts[state_view.act_index].normal_visited;
            const auto run_clock = state_view.clock; const auto run_boundary = state_view.boundary;
            state_view.combat = std::move(entering_combat);
            state_view.clock = run_clock; state_view.boundary = run_boundary;
            state_view.phase = RunPhase::combat;
        } else if (!return_from_room(state_view)) {
            state_view.phase = RunPhase::invalid;
            return {StepStatus::engine_error, state_view.boundary, Restriction::invalid_state};
        }
        ++state_view.boundary.value;
        if (state_view.phase == RunPhase::combat) {
            scratch.trace.emit(*state_view.combat, {CombatEventKind::combat_updated});
        }
        return {StepStatus::awaiting_input, state_view.boundary};
    } catch (const std::bad_alloc&) { state_view.phase = RunPhase::invalid; return {StepStatus::resource_limit, state_view.boundary, Restriction::capacity}; } catch (const std::length_error&) { state_view.phase = RunPhase::invalid; return {StepStatus::resource_limit, state_view.boundary, Restriction::capacity}; }
}
} // namespace
StepResult apply_input(RunState& state, const PlayerInput& input, Scratch& scratch) {
    if (!valid_creature_storage(state.data, state.overflow) || !valid_room_context(state.view())) return {StepStatus::engine_error, state.data.boundary, Restriction::invalid_state};
    if (const auto* run = std::get_if<RunInput>(&input)) return apply_run_transition(state, *run, scratch);
    return apply_combat_transition(state, std::get<CombatInput>(input), scratch);
}
} // namespace sts2::engine
