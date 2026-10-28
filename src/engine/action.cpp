#include "sts2/engine/action.hpp"

namespace sts2::engine {
namespace {
bool needs_automatic_transition(const RunState& state) {
    auto state_view = state.view();
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
    auto state_view = state.view();
    if (const auto* run = std::get_if<RunInput>(&input)) {
        if (run->expected_boundary != state_view.boundary) return {StepStatus::invalid_action, state_view.boundary, Restriction::stale_boundary};
        if (!is_canonical(*run)) return {StepStatus::invalid_action, state_view.boundary, Restriction::malformed_action};
        if (run->kind == RunInputKind::proceed || needs_automatic_transition(state)) return {StepStatus::invalid_action, state_view.boundary, Restriction::wrong_phase};
    }
    return apply_input(state, input, scratch);
}
}
std::size_t ActionSpaceView::size() const noexcept {
    if (!valid_data_storage(state_.data, state_.overflow)) return 0;
    if (state_.phase == CombatPhase::play) return state_.zone(Zone::hand).size() + 1;
    if (state_.phase == CombatPhase::discard_choice) return state_.zone(Zone::hand).size();
    return 0;
}
std::optional<ActionView> ActionSpaceView::at(std::size_t index) const noexcept {
    if (index >= size()) return std::nullopt;
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
Restriction validate_action(const RunState& state, const PlayerInput& input) {
    auto state_view = state.view();
    if (const auto* run = std::get_if<RunInput>(&input); run && run->kind == RunInputKind::travel) {
        if (run->expected_boundary != state_view.boundary) return Restriction::stale_boundary;
        if (!is_canonical(*run)) return Restriction::malformed_action;
        if (state_view.phase != RunPhase::map) return Restriction::wrong_phase;
        const auto& map = *state_view.maps[state_view.act_index];
        const auto destination = map.at(run->col, run->row);
        if (destination == no_node || !map.nodes[state_view.current].children.contains(destination)) return Restriction::invalid_selection;
        // Encounter assignments are hidden until entry. Public validation
        // reports visible legality; execution checks capability before mutation.
        return Restriction::none;
    }
    // Reuse authoritative validation, including native unsuccessful attempts.
    // Speculative work is isolated from complete state, clocks and RNG streams.
    auto candidate = state; Scratch scratch;
    return submit_action(candidate, input, scratch).reason;
}
std::vector<PlayerAction> get_actions(const RunState& state) {
    auto state_view = state.view();
    std::vector<PlayerAction> result;
    const auto add = [&](RunInputKind kind, std::uint32_t option = 0, int col = 0, int row = 0) {
        PlayerInput input = RunInput{state_view.boundary, kind, option, col, row};
        result.push_back({input, validate_action(state, input)});
    };
    if (state_view.phase == RunPhase::neow) {
        for (std::uint32_t i = 0; i < state_view.neow_options.size(); ++i) add(RunInputKind::choose_option, i);
    } else if (state_view.phase == RunPhase::map) {
        const auto& map = *state_view.maps[state_view.act_index];
        for (auto id : map.nodes[state_view.current].children.values()) add(RunInputKind::travel, 0, map.nodes[id].col, map.nodes[id].row);
    } else if (state_view.phase == RunPhase::rewards && state_view.rewards && !state_view.rewards->resolved) {
        const auto& rewards = *state_view.rewards;
        if (rewards.selecting_card) {
            for (std::uint32_t i = 0; i < rewards.cards.size(); ++i) add(RunInputKind::choose_reward_card, i);
            add(RunInputKind::reward_alternative);
        } else {
            if (!rewards.gold_taken) add(RunInputKind::take_reward, 0);
            if (rewards.potion != no_content && !rewards.potion_taken) add(RunInputKind::take_reward, 1);
            if (!rewards.card_taken) add(RunInputKind::take_reward, rewards.potion == no_content ? 1 : 2);
            add(RunInputKind::skip_rewards);
        }
    } else if (state_view.phase == RunPhase::combat && state_view.combat) {
        const ActionSpaceView combat(*state_view.combat);
        for (std::size_t i = 0; i < combat.size(); ++i) {
            const auto action = *combat.at(i); result.push_back({action.input, action.restriction});
        }
    }
    if ((state_view.phase == RunPhase::map || state_view.phase == RunPhase::rewards) && !(state_view.rewards && state_view.rewards->selecting_card))
        for (std::uint32_t i = 0; i < state_view.potions.size(); ++i)
            if (state_view.potions[i] != no_content) add(RunInputKind::discard_potion, i);
    return result;
}
StepResult advance_to_decision(RunState& state, Scratch& scratch) {
    auto state_view = state.view();
    while (needs_automatic_transition(state)) {
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
