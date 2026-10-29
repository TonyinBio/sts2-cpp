#include "sts2/engine/action.hpp"
#include "run_flow.hpp"
#include <algorithm>

namespace sts2::engine {
namespace {
bool needs_automatic_transition(ConstRunView state_view) {
    return state_view.phase == RunPhase::neow_done ||
        (state_view.phase == RunPhase::combat_done && !state_view.direct_combat) ||
        (state_view.phase == RunPhase::rewards && state_view.rewards && state_view.rewards->resolved);
}
bool is_canonical(const RunInput& input) {
    if (input.kind > RunInputKind::discard_potion) return false;
    if (input.kind == RunInputKind::travel) return input.option == 0;
    if (input.col != 0 || input.row != 0) return false;
    if (input.kind == RunInputKind::skip_rewards || input.kind == RunInputKind::proceed || input.kind == RunInputKind::abandon)
        return input.option == 0;
    return true;
}
StepResult submit_action(RunState& state, const PlayerInput& input, Scratch& scratch) {
    if (!valid_creature_storage(state.data, state.overflow)) return {StepStatus::engine_error, state.data.boundary, Restriction::invalid_state};
    auto state_view = state.view();
    if (const auto* run = std::get_if<RunInput>(&input)) {
        if (run->expected_boundary != state_view.boundary) return {StepStatus::invalid_action, state_view.boundary, Restriction::stale_boundary};
        if (!is_canonical(*run)) return {StepStatus::invalid_action, state_view.boundary, Restriction::malformed_action};
        if (run->kind == RunInputKind::proceed || needs_automatic_transition(state_view)) return {StepStatus::invalid_action, state_view.boundary, Restriction::wrong_phase};
    }
    return apply_input(state, input, scratch);
}
}
std::size_t ActionSpaceView::size() const noexcept {
    if (!valid_data_storage(state_.data, state_.overflow)) return 0;
    if (state_.phase == CombatPhase::play) return state_.zone(Zone::hand).size() + 1;
    if (state_.phase == CombatPhase::discard_choice) return state_.zone(Zone::hand).size();
    if (state_.phase == CombatPhase::card_choice) return state_.selection_candidates.size() + !state_.selection.automatic_at_maximum;
    return 0;
}
std::optional<ActionView> ActionSpaceView::at(std::size_t index) const noexcept {
    if (index >= size()) return std::nullopt;
    if (state_.phase == CombatPhase::card_choice) {
        CombatInput input{state_.boundary, CombatInputKind::confirm_selection, {}, false, state_.selection.id};
        if (index < state_.selection_candidates.size()) {
            input.card = state_.selection_candidates[index];
            input.kind = std::find(state_.selection_order.begin(), state_.selection_order.end(), input.card) == state_.selection_order.end() ? CombatInputKind::select_card : CombatInputKind::deselect_card;
        }
        return ActionView{input, validate_combat(state_, input)};
    }
    const auto& hand = state_.zone(Zone::hand);
    CombatInput input{state_.boundary, CombatInputKind::end_turn};
    if (index < hand.size()) {
        input.card = hand[index];
        if (state_.phase == CombatPhase::discard_choice) input.kind = CombatInputKind::select_card;
        else {
            input.kind = CombatInputKind::play_card;
            const auto* instance = state_.cards.get(input.card);
            const auto* def = instance ? Ruleset::pinned().card(instance->definition) : nullptr;
            input.target_enemy = def && def->enemy_target;
        }
    }
    return ActionView{input, validate_combat(state_, input)};
}
namespace {
Restriction validate_visible_input(ConstRunView view, const PlayerInput& input) noexcept {
    if (const auto* run = std::get_if<RunInput>(&input)) {
        if (run->expected_boundary != view.boundary) return Restriction::stale_boundary;
        if (!is_canonical(*run)) return Restriction::malformed_action;
        if (run->kind == RunInputKind::proceed || needs_automatic_transition(view)) return Restriction::wrong_phase;
        flow::RunActionPlan plan;
        const auto checked = flow::plan_run_input(view, *run, plan, flow::ValidationMode::visible);
        if (checked.reason != Restriction::none) return checked.reason;
        // Travel previews never expose hidden room capability or operational work.
        if (run->kind == RunInputKind::travel) return Restriction::none;
    } else {
        if (view.phase != RunPhase::combat || !view.combat) return Restriction::wrong_phase;
        const auto restriction = validate_combat(*view.combat, std::get<CombatInput>(input));
        if (restriction != Restriction::none) return restriction;
    }
    if (view.boundary.value == UINT32_MAX) return Restriction::capacity;
    auto clock = view.clock;
    return clock.advance_action() ? Restriction::none : Restriction::clock_exhausted;
}
}
Restriction validate_action(const RunState& state, const PlayerInput& input) noexcept {
    if (!valid_creature_storage(state.data, state.overflow)) return Restriction::invalid_state;
    if (state.data.phase != RunPhase::uninitialized && state.data.phase != RunPhase::invalid && !valid_run(state)) return Restriction::invalid_state;
    return validate_visible_input(state.view(), input);
}
RunActionSpaceView::RunActionSpaceView(const RunState& state) noexcept
    : state_(state), boundary_(state.data.boundary), valid_(valid_run(state)) {}
std::size_t RunActionSpaceView::size() const noexcept {
    if (!valid_ || boundary_ != state_.data.boundary) return 0;
    const auto view = state_.view();
    std::size_t count = 0;
    if (view.phase == RunPhase::room) count = 2;
    else if (view.phase == RunPhase::neow) count = view.neow_options.size();
    else if (view.phase == RunPhase::map) count = view.maps[view.act_index]->nodes[view.current].children.values().size();
    else if (view.phase == RunPhase::combat && view.combat) return ActionSpaceView(*view.combat).size();
    else if (view.phase == RunPhase::rewards && view.rewards && !view.rewards->resolved) {
        const auto& rewards = *view.rewards;
        if (rewards.selecting_card) count = rewards.cards.size() + 1;
        else count = !rewards.gold_taken + (rewards.potion != no_content && !rewards.potion_taken) + !rewards.card_taken + 1;
    }
    if ((view.phase == RunPhase::map || view.phase == RunPhase::rewards) && !(view.rewards && view.rewards->selecting_card))
        for (auto potion : view.potions) count += potion != no_content;
    return count;
}
std::optional<PlayerAction> RunActionSpaceView::at(std::size_t index) const noexcept {
    if (index >= size()) return std::nullopt;
    const auto view = state_.view();
    if (view.phase == RunPhase::combat && view.combat) {
        const auto action = ActionSpaceView(*view.combat).at(index);
        if (!action) return std::nullopt;
        return PlayerAction{action->input, action->restriction};
    }
    std::optional<PlayerAction> result;
    const auto offer = [&](RunInputKind kind, std::uint32_t option = 0, int col = 0, int row = 0) {
        if (result) return;
        if (index-- != 0) return;
        PlayerInput input = RunInput{view.boundary, kind, option, col, row};
        result = PlayerAction{input, validate_visible_input(view, input)};
    };
    if (view.phase == RunPhase::room) {
        offer(RunInputKind::choose_option, 0); offer(RunInputKind::choose_option, 1);
    } else if (view.phase == RunPhase::neow) {
        for (std::uint32_t i = 0; i < view.neow_options.size(); ++i) offer(RunInputKind::choose_option, i);
    } else if (view.phase == RunPhase::map) {
        const auto& map = *view.maps[view.act_index];
        for (auto id : map.nodes[view.current].children.values()) offer(RunInputKind::travel, 0, map.nodes[id].col, map.nodes[id].row);
    } else if (view.phase == RunPhase::rewards && view.rewards && !view.rewards->resolved) {
        const auto& rewards = *view.rewards;
        if (rewards.selecting_card) {
            for (std::uint32_t i = 0; i < rewards.cards.size(); ++i) offer(RunInputKind::choose_reward_card, i);
            offer(RunInputKind::reward_alternative);
        } else {
            if (!rewards.gold_taken) offer(RunInputKind::take_reward, 0);
            if (rewards.potion != no_content && !rewards.potion_taken) offer(RunInputKind::take_reward, 1);
            if (!rewards.card_taken) offer(RunInputKind::take_reward, rewards.potion == no_content ? 1 : 2);
            offer(RunInputKind::skip_rewards);
        }
    }
    if ((view.phase == RunPhase::map || view.phase == RunPhase::rewards) && !(view.rewards && view.rewards->selecting_card))
        for (std::uint32_t i = 0; i < view.potions.size(); ++i) if (view.potions[i] != no_content) offer(RunInputKind::discard_potion, i);
    return result;
}
std::vector<PlayerAction> get_actions(const RunState& state) {
    const RunActionSpaceView offered(state);
    std::vector<PlayerAction> actions; actions.reserve(offered.size());
    for (std::size_t i = 0; i < offered.size(); ++i) actions.push_back(*offered.at(i));
    return actions;
}
StepResult advance_to_decision(RunState& state, Scratch& scratch) {
    if (!valid_creature_storage(state.data, state.overflow)) return {StepStatus::engine_error, state.data.boundary, Restriction::invalid_state};
    auto state_view = state.view();
    while (needs_automatic_transition(state_view)) {
        const auto result = apply_input(state, RunInput{state_view.boundary, RunInputKind::proceed}, scratch);
        if (result.status != StepStatus::awaiting_input) {
            // The initiating player input has already executed. A missing
            // automatic capability cannot be published as a valid partial stop.
            state_view.phase = RunPhase::invalid;
            return {StepStatus::engine_error, state_view.boundary, result.reason};
        }
    }
    if (state_view.phase == RunPhase::invalid || state_view.phase == RunPhase::uninitialized)
        return {StepStatus::engine_error, state_view.boundary, Restriction::invalid_state};
    if (state_view.phase == RunPhase::complete) return {StepStatus::player_died, state_view.boundary};
    if (state_view.phase == RunPhase::combat_done) return {StepStatus::combat_won, state_view.boundary};
    return {StepStatus::awaiting_input, state_view.boundary};
}
StepResult apply_action(RunState& state, const PlayerInput& input, Scratch& scratch) {
    const auto result = submit_action(state, input, scratch);
    if (result.status != StepStatus::awaiting_input && result.status != StepStatus::combat_won) return result;
    return advance_to_decision(state, scratch);
}
} // namespace sts2::engine
