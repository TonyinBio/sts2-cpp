#include "run_flow.hpp"
#include <algorithm>
#include <climits>

namespace sts2::engine::flow {
std::uint32_t highest_combat_generation(ConstRunView state) noexcept {
    auto highest = state.combat_generation;
    if (state.combat) highest = std::max(highest, state.combat->cards.data.highest_generation);
    return highest;
}
std::uint32_t next_combat_generation(ConstRunView state) noexcept {
    const auto highest = highest_combat_generation(state);
    return highest == UINT32_MAX ? 0 : highest + 1;
}
bool supported_combat_inventory(ConstRunView state) {
    for (auto potion : state.potions)
        if (potion != no_content && SetupRules::name(potion) != "POTION.POTION_OF_BINDING") return false;
    // Inventory hooks are supported only for the starting relic and Golden Pearl.
    for (auto relic : state.relics)
        if (SetupRules::name(relic) != "RELIC.BURNING_BLOOD" && SetupRules::name(relic) != "RELIC.GOLDEN_PEARL" &&
            SetupRules::name(relic) != "RELIC.CHARONS_ASHES" && SetupRules::name(relic) != "RELIC.JOSS_PAPER") return false;
    return true;
}
StepResult plan_run_input(ConstRunView state_view, const RunInput& input, RunActionPlan& plan, ValidationMode mode) {
    if (!valid_room_context(state_view)) return {StepStatus::engine_error, state_view.boundary, Restriction::invalid_state};
    if (state_view.phase == RunPhase::uninitialized || state_view.phase == RunPhase::invalid)
        return {StepStatus::engine_error, state_view.boundary, Restriction::invalid_state};
    if (input.expected_boundary != state_view.boundary) return {StepStatus::invalid_action, state_view.boundary, Restriction::stale_boundary};
    auto& entering_generation = plan.entering_generation;
    auto& generate_rewards = plan.generate_rewards;
    auto& destination = plan.destination;
    auto& potion_slot = plan.potion_slot; potion_slot = state_view.potions.size();
    auto& next_card_instance = plan.next_card_instance;
    const auto reward_kind = plan.reward_kind = input.kind == RunInputKind::take_reward || input.kind == RunInputKind::skip_rewards ||
        input.kind == RunInputKind::choose_reward_card || input.kind == RunInputKind::reward_alternative;
    if (reward_kind) {
        if (state_view.phase != RunPhase::rewards || !state_view.rewards || state_view.rewards->resolved)
            return {StepStatus::invalid_action, state_view.boundary, Restriction::wrong_phase};
        const auto& rewards = *state_view.rewards;
        if (rewards.selecting_card) {
            if (input.kind == RunInputKind::choose_reward_card) {
                if (input.option >= rewards.cards.size()) return {StepStatus::invalid_action, state_view.boundary, Restriction::invalid_selection};
                const auto id = SetupRules::name(rewards.cards[input.option].definition);
                if (id != "CARD.TRUE_GRIT" && id != "CARD.JUGGLING" && id != "CARD.RAGE")
                    return {StepStatus::unsupported_content, state_view.boundary, Restriction::unsupported_setup};
                if (state_view.deck.size() >= 4096) return {StepStatus::resource_limit, state_view.boundary, Restriction::capacity};
                for (const auto& existing : state_view.deck) {
                    if (existing.instance == UINT32_MAX) return {StepStatus::resource_limit, state_view.boundary, Restriction::capacity};
                    next_card_instance = std::max(next_card_instance, existing.instance + 1);
                }
            } else if (input.kind != RunInputKind::reward_alternative || input.option != 0)
                return {StepStatus::invalid_action, state_view.boundary, Restriction::invalid_selection};
        } else if (input.kind == RunInputKind::take_reward) {
            const auto card_index = rewards.potion == no_content ? 1u : 2u;
            if (input.option > card_index || (input.option == 0 && rewards.gold_taken) ||
                (input.option == 1 && card_index == 2 && rewards.potion_taken) || (input.option == card_index && rewards.card_taken))
                return {StepStatus::invalid_action, state_view.boundary, Restriction::invalid_selection};
            if (input.option == 0 && state_view.gold > INT32_MAX - rewards.gold)
                return {StepStatus::resource_limit, state_view.boundary, Restriction::capacity};
            if (input.option == 1 && card_index == 2) {
                for (std::size_t i = 0; i < state_view.potions.size(); ++i) if (state_view.potions[i] == no_content) { potion_slot = i; break; }
            }
        } else if (input.kind != RunInputKind::skip_rewards)
            return {StepStatus::invalid_action, state_view.boundary, Restriction::wrong_phase};
    } else if (input.kind == RunInputKind::discard_potion) {
        if (state_view.phase != RunPhase::rewards && state_view.phase != RunPhase::map)
            return {StepStatus::unsupported_content, state_view.boundary, Restriction::unsupported_setup};
        if (state_view.rewards && state_view.rewards->selecting_card) return {StepStatus::invalid_action, state_view.boundary, Restriction::wrong_phase};
        if (input.option >= state_view.potions.size() || state_view.potions[input.option] == no_content)
            return {StepStatus::invalid_action, state_view.boundary, Restriction::invalid_selection};
    } else
    if (input.kind == RunInputKind::choose_option) {
        if (state_view.phase == RunPhase::room) {
            const auto* parent = std::get_if<ParentInteractionState>(&state_view.room);
            if (!parent || parent->stage != ParentRoomStage::choosing || parent->pending_gold < 0) return {StepStatus::engine_error, state_view.boundary, Restriction::invalid_state};
            if (input.option >= 2) return {StepStatus::invalid_action, state_view.boundary, Restriction::invalid_selection};
            if (input.option == 0 && state_view.gold > INT32_MAX - parent->pending_gold) return {StepStatus::resource_limit, state_view.boundary, Restriction::capacity};
            return {StepStatus::awaiting_input, state_view.boundary};
        }
        if (state_view.phase != RunPhase::neow) return {StepStatus::invalid_action, state_view.boundary, Restriction::wrong_phase};
        if (input.option >= state_view.neow_options.size()) return {StepStatus::invalid_action, state_view.boundary, Restriction::invalid_selection};
        if (state_view.gold > INT32_MAX - 150)
            return {StepStatus::resource_limit, state_view.boundary, Restriction::capacity};
        if (SetupRules::name(state_view.neow_options[input.option]) != "RELIC.GOLDEN_PEARL")
            return {StepStatus::unsupported_content, state_view.boundary, Restriction::unsupported_setup};
    } else if (input.kind == RunInputKind::proceed) {
        if (state_view.phase == RunPhase::combat_done && !state_view.direct_combat) {
            generate_rewards = true;
        } else if (state_view.phase != RunPhase::neow_done && !(state_view.phase == RunPhase::rewards && state_view.rewards && state_view.rewards->resolved)) return {StepStatus::invalid_action, state_view.boundary, Restriction::wrong_phase};
    } else if (input.kind == RunInputKind::travel) {
        if (state_view.phase != RunPhase::map) return {StepStatus::invalid_action, state_view.boundary, Restriction::wrong_phase};
        const auto& map = *state_view.maps[state_view.act_index]; const auto next = map.at(input.col, input.row);
        if (next == no_node || !map.nodes[state_view.current].children.contains(next)) return {StepStatus::invalid_action, state_view.boundary, Restriction::invalid_selection};
        if (mode == ValidationMode::visible) { destination = next; return {StepStatus::awaiting_input, state_view.boundary}; }
        if (map.nodes[next].kind != MapKind::monster || state_view.ascension != 0 ||
            SetupRules::name(state_view.character) != "CHARACTER.IRONCLAD")
            return {StepStatus::unsupported_content, state_view.boundary, Restriction::unsupported_setup};
        if (next_combat_generation(state_view) == 0)
            return {StepStatus::resource_limit, state_view.boundary, Restriction::capacity};
        const auto& rooms = state_view.acts[state_view.act_index];
        if (rooms.normal_visited >= rooms.normal.size() || SetupRules::name(rooms.normal[rooms.normal_visited]) != "ENCOUNTER.SHRINKER_BEETLE_WEAK")
            return {StepStatus::unsupported_content, state_view.boundary, Restriction::unsupported_setup};
        if (!supported_combat_inventory(state_view))
            return {StepStatus::unsupported_content, state_view.boundary, Restriction::unsupported_setup};
        entering_generation = next_combat_generation(state_view);
        destination = next;
    } else return {StepStatus::unsupported_content, state_view.boundary, Restriction::unsupported_setup};
    return {StepStatus::awaiting_input, state_view.boundary};
}
bool valid_room_context(ConstRunView state) noexcept {
    if (state.rewards && state.rewards->return_to > RewardReturn::parent_room) return false;
    if (std::holds_alternative<std::monostate>(state.room))
        return state.phase != RunPhase::room && (!state.rewards || state.rewards->return_to == RewardReturn::map);
    const auto* parent = std::get_if<ParentInteractionState>(&state.room);
    if (!parent || parent->pending_gold < 0 || state.direct_combat || state.combat_generation == 0) return false;
    switch (parent->stage) {
    case ParentRoomStage::child_combat: return state.phase == RunPhase::combat && state.combat && !state.rewards;
    case ParentRoomStage::child_rewards:
        return !state.combat && ((state.phase == RunPhase::combat_done && !state.rewards) ||
            (state.phase == RunPhase::rewards && state.rewards && state.rewards->return_to == RewardReturn::parent_room));
    case ParentRoomStage::choosing: return state.phase == RunPhase::room && !state.combat && !state.rewards;
    }
    return false;
}
void apply_reward_input(RunView state_view, const RunInput& input, const RunActionPlan& plan) {
    const auto potion_slot = plan.potion_slot;
    const auto next_card_instance = plan.next_card_instance;
    auto& rewards = *state_view.rewards;
    if (input.kind == RunInputKind::take_reward) {
        const auto card_index = rewards.potion == no_content ? 1u : 2u;
        if (input.option == 0) { state_view.gold += rewards.gold; rewards.gold_taken = true; }
        else if (input.option == card_index) rewards.selecting_card = true;
        else if (potion_slot < static_cast<std::size_t>(state_view.potion_slots)) {
            if (potion_slot == state_view.potions.size()) state_view.potions.push_back(rewards.potion);
            else state_view.potions[potion_slot] = rewards.potion;
            rewards.potion_taken = true;
        }
    } else if (input.kind == RunInputKind::choose_reward_card) {
        auto card = rewards.cards[input.option]; card.instance = next_card_instance;
        card.floor_added = static_cast<std::int32_t>(state_view.visited.size()); state_view.deck.push_back(card);
        rewards.selected_card = static_cast<std::uint8_t>(input.option); rewards.card_taken = true; rewards.selecting_card = false;
    } else if (input.kind == RunInputKind::reward_alternative) rewards.selecting_card = false;
    else { rewards.skipped = true; rewards.resolved = true; }
    if (rewards.gold_taken && (rewards.potion == no_content || rewards.potion_taken) && rewards.card_taken) rewards.resolved = true;

}
void apply_room_option(RunView state, const RunInput& input) {
    if (state.phase == RunPhase::room) {
        const auto gold = std::get<ParentInteractionState>(state.room).pending_gold;
        if (input.option == 0) state.gold += gold;
        state.room = std::monostate{}; state.phase = RunPhase::map;
    } else {
        state.selected_neow = state.neow_options[input.option];
        state.relics.push_back(state.selected_neow); state.gold += 150;
        state.phase = RunPhase::neow_done;
    }
}
void child_combat_finished(RunView state, bool won) noexcept {
    if (auto* parent = std::get_if<ParentInteractionState>(&state.room)) {
        if (won) parent->stage = ParentRoomStage::child_rewards;
        else state.room = std::monostate{};
    }
}
bool return_from_room(RunView state) noexcept {
    if (state.phase == RunPhase::neow_done) { state.phase = RunPhase::map; return true; }
    if (!state.rewards || !state.rewards->resolved) return false;
    if (state.rewards->return_to == RewardReturn::map) { state.phase = RunPhase::map; return true; }
    auto* parent = std::get_if<ParentInteractionState>(&state.room);
    if (!parent || parent->stage != ParentRoomStage::child_rewards) return false;
    state.rewards.reset(); parent->stage = ParentRoomStage::choosing; state.phase = RunPhase::room;
    return true;
}
}
