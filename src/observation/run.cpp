#include "sts2/observation/run.hpp"
#include <algorithm>
#include "sts2/core/sort.hpp"

namespace sts2::engine {
namespace {
std::optional<std::int32_t> card_cost(std::string_view model) {
    for (const auto& definition : Ruleset::pinned().cards())
        if (definition.model_id == model) return definition.cost;
    if (model == "CARD.TRUE_GRIT" || model == "CARD.JUGGLING") return 1;
    if (model == "CARD.RAGE") return 0;
    return std::nullopt;
}
bool forced(const RunState& state) {
    return state.phase == RunPhase::neow_done ||
        (state.phase == RunPhase::combat_done && !state.direct_combat) ||
        (state.phase == RunPhase::rewards && state.rewards && state.rewards->resolved);
}
bool canonical(const RunInput& input) {
    if (input.kind > RunInputKind::discard_potion) return false;
    if (input.kind == RunInputKind::travel) return input.option == 0;
    if (input.col != 0 || input.row != 0) return false;
    if (input.kind == RunInputKind::skip_rewards || input.kind == RunInputKind::proceed || input.kind == RunInputKind::abandon)
        return input.option == 0;
    return true;
}
StepResult submit(RunState& state, const PlayerInput& input, Scratch& scratch) {
    if (const auto* run = std::get_if<RunInput>(&input)) {
        if (run->expected_boundary != state.boundary) return {StepStatus::invalid_action, state.boundary, Restriction::stale_boundary};
        if (!canonical(*run)) return {StepStatus::invalid_action, state.boundary, Restriction::malformed_action};
        if (run->kind == RunInputKind::proceed || forced(state)) return {StepStatus::invalid_action, state.boundary, Restriction::wrong_phase};
        return apply_run(state, *run);
    }
    return apply_combat(state, std::get<Input>(input), scratch);
}
}
RunObservation observe(const RunState& state) {
    RunObservation view{};
    view.boundary = state.boundary; view.phase = state.phase;
    view.valid = state.phase != RunPhase::invalid && state.phase != RunPhase::uninitialized;
    if (!view.valid) return view;
    view.character = SetupRules::name(state.setup.character); view.seed = state.setup.seed;
    view.ascension = state.setup.ascension; view.act = state.act_index + 1;
    view.hp = state.setup.hp; view.max_hp = state.setup.max_hp; view.gold = state.setup.gold;
    view.potion_capacity = state.setup.potion_slots;
    for (const auto& card : state.setup.deck) view.deck.push_back({card.instance, SetupRules::name(card.definition), card.upgrade, card_cost(SetupRules::name(card.definition))});
    for (std::size_t i = 0; i < state.setup.relics.size(); ++i) view.relics.push_back({static_cast<std::uint32_t>(i), SetupRules::name(state.setup.relics[i])});
    for (std::size_t i = 0; i < state.setup.potions.size(); ++i) view.potions.push_back({static_cast<std::uint32_t>(i), SetupRules::name(state.setup.potions[i])});
    if (state.phase == RunPhase::neow)
        for (std::size_t i = 0; i < state.neow_options.size(); ++i) view.options.push_back({static_cast<std::uint32_t>(i), SetupRules::name(state.neow_options[i])});
    switch (state.phase) {
    case RunPhase::neow: view.prompt = PublicPrompt::neow; break;
    case RunPhase::map: view.prompt = PublicPrompt::map; break;
    case RunPhase::combat: view.prompt = PublicPrompt::combat; break;
    case RunPhase::rewards: view.prompt = PublicPrompt::rewards; break;
    default: break;
    }
    // Only the generated current map is public. No room assignment or RNG fields.
    if (state.maps[state.act_index]) {
        const auto& map = *state.maps[state.act_index];
        auto nodes = map.points();
        nodes.insert(nodes.begin(), map.start);
        if (map.boss != no_node) nodes.push_back(map.boss);
        if (map.second_boss != no_node) nodes.push_back(map.second_boss);
        for (const auto id : nodes) {
            const auto& node = map.nodes[id];
            view.map.push_back({id, node.col, node.row, node.kind, node.children.values(),
                std::find(state.visited.begin(), state.visited.end(), id) != state.visited.end(),
                id == state.current, state.phase == RunPhase::map && map.nodes[state.current].children.contains(id)});
        }
        view.path = state.visited;
    }
    if (state.phase == RunPhase::rewards && state.rewards) {
        const auto& rewards = *state.rewards;
        view.selecting_reward_card = rewards.selecting_card;
        view.can_skip_rewards = !rewards.selecting_card && !rewards.resolved;
        view.can_skip_card_reward = rewards.selecting_card;
        if (rewards.selecting_card) {
            view.prompt = PublicPrompt::card_reward;
            view.minimum_selection = view.maximum_selection = 1;
        }
        view.rewards.push_back({0, RewardKind::gold, rewards.gold, {}, rewards.gold_taken});
        if (rewards.potion != no_content) view.rewards.push_back({1, RewardKind::potion, 0, SetupRules::name(rewards.potion), rewards.potion_taken});
        view.rewards.push_back({static_cast<std::uint32_t>(view.rewards.size()), RewardKind::cards, 0, {}, rewards.card_taken});
        if (rewards.selecting_card)
            for (std::size_t i = 0; i < rewards.cards.size(); ++i) view.reward_cards.push_back({static_cast<std::uint32_t>(i), SetupRules::name(rewards.cards[i].definition), rewards.cards[i].upgrade, card_cost(SetupRules::name(rewards.cards[i].definition))});
    }
    if (state.phase == RunPhase::combat && state.combat) {
        view.combat = observe(*state.combat);
        for (auto handle : view.combat->hand) {
            if (auto card = observe_card(*state.combat, handle)) view.hand_cards.push_back(*card);
        }
        if (state.combat->phase == Phase::discard_choice) {
            view.prompt = PublicPrompt::discard;
            view.minimum_selection = view.maximum_selection = 1;
        }
        if (view.combat->selection) {
            view.prompt = PublicPrompt::card_selection;
            view.minimum_selection = view.combat->selection->minimum;
            view.maximum_selection = view.combat->selection->maximum;
            view.submits_immediately = view.combat->selection->automatic_at_maximum;
        }
        const auto project_pile = [&](Zone zone, std::vector<CardView>& pile) {
            for (const auto handle : state.combat->zone(zone))
                if (const auto card = inspect_card(*state.combat, handle)) pile.push_back(*card);
        };
        project_pile(Zone::draw, view.draw); project_pile(Zone::discard, view.discard); project_pile(Zone::exhaust, view.exhaust);
        // NCardPileScreen: raw rarity then ordinal model entry. All four
        // currently executable combat definitions have native Basic rarity.
        // Match .NET equal-key sorting rather than exposing backing draw order.
        game_sort(std::span(view.draw), [](const auto& a, const auto& b) { return a.model_id < b.model_id; });
    }
    return view;
}
Restriction validate_player(const RunState& state, const PlayerInput& input) {
    if (const auto* run = std::get_if<RunInput>(&input); run && run->kind == RunInputKind::travel) {
        if (run->expected_boundary != state.boundary) return Restriction::stale_boundary;
        if (!canonical(*run)) return Restriction::malformed_action;
        if (state.phase != RunPhase::map) return Restriction::wrong_phase;
        const auto& map = *state.maps[state.act_index];
        const auto destination = map.at(run->col, run->row);
        if (destination == no_node || !map.nodes[state.current].children.contains(destination)) return Restriction::invalid_selection;
        // Encounter assignments are hidden until entry. Public validation
        // reports visible legality; execution checks capability before mutation.
        return Restriction::none;
    }
    // Reuse authoritative validation, including native unsuccessful attempts.
    // Speculative work is isolated from complete state, clocks and RNG streams.
    auto candidate = state; Scratch scratch;
    return submit(candidate, input, scratch).reason;
}
std::vector<PlayerAction> actions(const RunState& state) {
    std::vector<PlayerAction> result;
    const auto add = [&](RunInputKind kind, std::uint32_t option = 0, int col = 0, int row = 0) {
        PlayerInput input = RunInput{state.boundary, kind, option, col, row};
        result.push_back({input, validate_player(state, input)});
    };
    if (state.phase == RunPhase::neow) {
        for (std::uint32_t i = 0; i < state.neow_options.size(); ++i) add(RunInputKind::choose_option, i);
    } else if (state.phase == RunPhase::map) {
        const auto& map = *state.maps[state.act_index];
        for (auto id : map.nodes[state.current].children.values()) add(RunInputKind::travel, 0, map.nodes[id].col, map.nodes[id].row);
    } else if (state.phase == RunPhase::rewards && state.rewards && !state.rewards->resolved) {
        const auto& rewards = *state.rewards;
        if (rewards.selecting_card) {
            for (std::uint32_t i = 0; i < rewards.cards.size(); ++i) add(RunInputKind::choose_reward_card, i);
            add(RunInputKind::reward_alternative);
        } else {
            if (!rewards.gold_taken) add(RunInputKind::take_reward, 0);
            if (rewards.potion != no_content && !rewards.potion_taken) add(RunInputKind::take_reward, 1);
            if (!rewards.card_taken) add(RunInputKind::take_reward, rewards.potion == no_content ? 1 : 2);
            add(RunInputKind::skip_rewards);
        }
    } else if (state.phase == RunPhase::combat && state.combat) {
        const ActionSpaceView combat(*state.combat);
        for (std::size_t i = 0; i < combat.size(); ++i) {
            const auto action = *combat.at(i); result.push_back({action.input, action.restriction});
        }
    }
    if ((state.phase == RunPhase::map || state.phase == RunPhase::rewards) && !(state.rewards && state.rewards->selecting_card))
        for (std::uint32_t i = 0; i < state.setup.potions.size(); ++i)
            if (state.setup.potions[i] != no_content) add(RunInputKind::discard_potion, i);
    return result;
}
StepResult advance_player(RunState& state, Scratch&) {
    while (forced(state)) {
        const auto result = apply_run(state, {state.boundary, RunInputKind::proceed});
        if (result.status != StepStatus::awaiting_input) {
            // The initiating player input has already executed. A missing
            // automatic capability cannot be published as a valid partial stop.
            state.phase = RunPhase::invalid;
            return {StepStatus::engine_error, state.boundary, result.reason};
        }
    }
    if (state.phase == RunPhase::invalid || state.phase == RunPhase::uninitialized)
        return {StepStatus::engine_error, state.boundary, Restriction::invalid_state};
    if (state.phase == RunPhase::complete) return {StepStatus::player_died, state.boundary};
    if (state.phase == RunPhase::combat_done) return {StepStatus::combat_won, state.boundary};
    return {StepStatus::awaiting_input, state.boundary};
}
StepResult apply_player(RunState& state, const PlayerInput& input, Scratch& scratch) {
    const auto result = submit(state, input, scratch);
    if (result.status != StepStatus::awaiting_input && result.status != StepStatus::combat_won) return result;
    return advance_player(state, scratch);
}
std::string describe(const RunObservation& view) {
    if (!view.valid) return "Simulation unavailable.";
    if (view.phase == RunPhase::neow) return "Choose a gift from Neow.";
    if (view.phase == RunPhase::map) return "Choose a route on the map.";
    if (view.selecting_reward_card) return "Choose one card to add to your deck, or skip.";
    if (view.phase == RunPhase::rewards) return "Claim rewards, or leave the remaining rewards.";
    if (view.phase == RunPhase::complete) return "Run ended.";
    if (view.prompt == PublicPrompt::discard) return "Choose one card to discard.";
    if (view.combat) return "Play a card or end your turn.";
    return "Combat won.";
}
DefinitionText describe(const VisibleCard& card) {
    DefinitionText text;
    for (const auto& definition : Ruleset::pinned().cards()) {
        if (definition.model_id != card.model_id) continue;
        CardView preview{{}, definition.model_id, definition.name, definition.cost, 0, 0, 0, card.upgrade != 0, definition.enemy_target};
        for (const auto& op : definition.program) {
            const auto value = op.base + (card.upgrade ? op.upgrade : 0);
            if (op.opcode == Opcode::damage) preview.damage = value;
            if (op.opcode == Opcode::block) preview.block = value;
            if (op.opcode == Opcode::vulnerable) preview.vulnerable = value;
            if (op.opcode == Opcode::strength) preview.strength = value;
            if (op.opcode == Opcode::weak) preview.weak = value;
        }
        return {std::string(definition.name), describe(preview), true};
    }
    if (card.model_id == "CARD.TRUE_GRIT")
        return {"True Grit", "Gain " + std::to_string(card.upgrade ? 9 : 7) + " Block. Exhaust " +
            (card.upgrade ? "a card from your hand." : "a random card from your hand."), true};
    if (card.model_id == "CARD.JUGGLING")
        return {"Juggling", std::string(card.upgrade ? "Innate. " : "") + "Each turn, add a copy of the third Attack you play to your hand.", true};
    if (card.model_id == "CARD.RAGE")
        return {"Rage", "Whenever you play an Attack this turn, gain " + std::to_string(card.upgrade ? 5 : 3) + " Block.", true};
    return describe(VisibleOption{card.instance, card.model_id});
}
DefinitionText describe(const VisibleOption& option) {
    if (option.model_id == "RELIC.GOLDEN_PEARL") return {"Golden Pearl", "Gain 150 Gold.", true};
    if (option.model_id == "RELIC.BURNING_BLOOD") return {"Burning Blood", "At the end of combat, heal 6 HP.", true};
    if (option.model_id == "POTION.POTION_OF_BINDING") return {"Potion of Binding", "Apply 1 Weak and 1 Vulnerable to all enemies.", true};
    return {std::string(option.model_id), "Description unavailable for this definition.", false};
}
std::string describe(Restriction restriction) {
    switch (restriction) {
    case Restriction::none: return {};
    case Restriction::insufficient_energy: return "Not enough Energy.";
    case Restriction::invalid_target: return "Choose an eligible target.";
    case Restriction::stale_boundary: return "This choice has expired.";
    case Restriction::unsupported_setup: return "This operation is not supported.";
    default: return "This action is unavailable.";
    }
}
} // namespace sts2::engine
