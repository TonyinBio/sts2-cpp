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
}
RunObservation observe(const RunState& state) {
    auto state_view = state.view();
    RunObservation view{};
    view.boundary = state_view.boundary; view.phase = state_view.phase;
    view.valid = state_view.phase != RunPhase::invalid && state_view.phase != RunPhase::uninitialized;
    if (!view.valid) return view;
    view.character = SetupRules::name(state_view.character); view.seed = state_view.seed;
    view.ascension = state_view.ascension; view.act = state_view.act_index + 1;
    view.hp = state_view.hp; view.max_hp = state_view.max_hp; view.gold = state_view.gold;
    view.potion_capacity = state_view.potion_slots;
    for (const auto& card : state_view.deck) view.deck.push_back({card.instance, SetupRules::name(card.definition), card.upgrade, card_cost(SetupRules::name(card.definition))});
    for (std::size_t i = 0; i < state_view.relics.size(); ++i) {
        const auto name = SetupRules::name(state_view.relics[i]);
        view.relics.push_back({static_cast<std::uint32_t>(i), name, name == "RELIC.JOSS_PAPER" ? std::optional(state_view.joss_paper_counter) : std::nullopt});
    }
    for (std::size_t i = 0; i < state_view.potions.size(); ++i) view.potions.push_back({static_cast<std::uint32_t>(i), SetupRules::name(state_view.potions[i])});
    if (state_view.phase == RunPhase::neow)
        for (std::size_t i = 0; i < state_view.neow_options.size(); ++i) view.options.push_back({static_cast<std::uint32_t>(i), SetupRules::name(state_view.neow_options[i])});
    switch (state_view.phase) {
    case RunPhase::neow: view.prompt = PublicPrompt::neow; break;
    case RunPhase::map: view.prompt = PublicPrompt::map; break;
    case RunPhase::combat: view.prompt = PublicPrompt::combat; break;
    case RunPhase::rewards: view.prompt = PublicPrompt::rewards; break;
    default: break;
    }
    // Only the generated current map is public. No room assignment or RNG fields.
    if (state_view.maps[state_view.act_index]) {
        const auto& map = *state_view.maps[state_view.act_index];
        auto nodes = map.points();
        nodes.insert(nodes.begin(), map.start);
        if (map.boss != no_node) nodes.push_back(map.boss);
        if (map.second_boss != no_node) nodes.push_back(map.second_boss);
        for (const auto id : nodes) {
            const auto& node = map.nodes[id];
            view.map.push_back({id, node.col, node.row, node.kind, node.children.values(),
                std::find(state_view.visited.begin(), state_view.visited.end(), id) != state_view.visited.end(),
                id == state_view.current, state_view.phase == RunPhase::map && map.nodes[state_view.current].children.contains(id)});
        }
        view.path.assign(state_view.visited.begin(), state_view.visited.end());
    }
    if (state_view.phase == RunPhase::rewards && state_view.rewards) {
        const auto& rewards = *state_view.rewards;
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
    if (state_view.phase == RunPhase::combat && state_view.combat) {
        view.combat = observe(*state_view.combat);
        for (auto handle : view.combat->hand) {
            if (auto card = observe_card(*state_view.combat, handle)) view.hand_cards.push_back(*card);
        }
        if (state_view.combat->phase == CombatPhase::discard_choice) {
            view.prompt = PublicPrompt::discard;
            view.minimum_selection = view.maximum_selection = 1;
        }
        if (view.combat->selection) {
            view.prompt = PublicPrompt::card_selection;
            view.minimum_selection = view.combat->selection->minimum;
            view.maximum_selection = view.combat->selection->maximum;
            view.submits_immediately = view.combat->selection->submits_immediately;
        }
        const auto project_pile = [&](Zone zone, std::vector<CardView>& pile) {
            for (const auto handle : state_view.combat->zone(zone))
                if (const auto card = inspect_card(*state_view.combat, handle)) pile.push_back(*card);
        };
        project_pile(Zone::draw, view.draw); project_pile(Zone::discard, view.discard); project_pile(Zone::exhaust, view.exhaust);
        // NCardPileScreen: raw rarity then ordinal model entry. All four
        // starter cards have native Basic rarity; scenario cards keep their native rarity.
        // Match .NET equal-key sorting rather than exposing backing draw order.
        game_sort(std::span(view.draw), [](const auto& a, const auto& b) { return a.rarity == b.rarity ? a.model_id < b.model_id : a.rarity < b.rarity; });
    }
    return view;
}
std::string describe(const RunObservation& view) {
    if (!view.valid) return "Simulation unavailable.";
    if (view.phase == RunPhase::neow) return "Choose a gift from Neow.";
    if (view.phase == RunPhase::map) return "Choose a route on the map.";
    if (view.selecting_reward_card) return "Choose one card to add to your deck, or skip.";
    if (view.phase == RunPhase::rewards) return "Claim rewards, or leave the remaining rewards.";
    if (view.phase == RunPhase::complete) return "Run ended.";
    if (view.prompt == PublicPrompt::discard) return "Choose one card to discard.";
    if (view.combat && view.combat->selection) {
        const auto& choice = *view.combat->selection;
        const char* effect = choice.effect == SelectionEffect::upgrade ? "upgrade" : choice.effect == SelectionEffect::topdeck ? "put on top of your draw pile" : "exhaust";
        if (choice.submits_immediately) return std::string("Choose a card to ") + effect + ".";
        if (choice.minimum == 0) return "Select up to " + std::to_string(choice.maximum) + " cards to " + effect + ", then confirm.";
        return std::string("Select a card to ") + effect + ", then confirm.";
    }
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
