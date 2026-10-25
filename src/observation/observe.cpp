#include "sts2/observation/observe.hpp"
#include <algorithm>

namespace sts2::engine {
ObservationView observe(const State& s) noexcept {
    const auto base_attack = s.move == BeetleMove::chomp ? 7 : s.move == BeetleMove::stomp ? 13 : 0;
    const auto attack = std::max(0, base_attack + s.enemy.strength) * (s.enemy.weak > 0 ? 75 : 100) / 100;
    ObservationView view{game_build, s.boundary, s.phase, s.player.hp, s.player.max_hp, s.player.block, s.energy,
        s.turn, s.enemy.hp, s.enemy.max_hp, s.enemy.vulnerable, attack, s.player.shrink, s.move,
        s.zone(Zone::hand), s.zone(Zone::draw).size(), s.zone(Zone::discard).size(), s.zone(Zone::exhaust).size(), s.player.strength, s.player.dexterity, s.player.weak, s.enemy.weak, {}};
    if (s.phase == Phase::card_choice) {
        const auto& p = s.selection;
        view.selection = SelectionView{p.id, p.candidates, p.selected, p.minimum, p.maximum, p.automatic_at_maximum, p.selected.size() >= p.minimum && p.selected.size() <= p.maximum};
    }
    return view;
}
std::optional<CardView> observe_card(const State& s, CardHandle h, const Ruleset& rules) noexcept {
    const auto& hand = s.zone(Zone::hand);
    if (std::find(hand.begin(), hand.end(), h) == hand.end()) return std::nullopt;
    return inspect_card(s, h, rules);
}
std::optional<CardView> inspect_card(const State& s, CardHandle h, const Ruleset& rules) noexcept {
    bool visible = false;
    for (const auto zone : {Zone::hand, Zone::draw, Zone::discard, Zone::exhaust}) {
        const auto& cards = s.zone(zone);
        visible = visible || std::find(cards.begin(), cards.end(), h) != cards.end();
    }
    if (!visible) return std::nullopt;
    const auto* instance = s.cards.get(h); if (!instance) return std::nullopt;
    const auto* def = rules.card(instance->definition); if (!def) return std::nullopt;
    CardView view{h, def->model_id, def->name, def->cost, 0, 0, 0, instance->upgraded, def->enemy_target};
    for (const auto& op : def->program) {
        const auto value = op.base + (instance->upgraded ? op.upgrade : 0);
        if (op.opcode == Opcode::damage) view.damage = static_cast<int>(std::int64_t(std::max(0, value + s.player.strength)) * (s.player.shrink ? 70 : 100) * (s.player.weak > 0 ? 75 : 100) * (s.enemy.vulnerable ? 150 : 100) / 1000000);
        else if (op.opcode == Opcode::block) view.block = std::max(0, value + s.player.dexterity);
        else if (op.opcode == Opcode::vulnerable) view.vulnerable = value;
        else if (op.opcode == Opcode::strength) view.strength = value;
        else if (op.opcode == Opcode::weak) view.weak = value;
    }
    view.exhaust = instance->definition == CardId::shockwave || instance->definition == CardId::tremble;
    view.power = instance->definition == CardId::inflame;
    return view;
}
std::string describe(const CardView& card) {
    std::string text;
    if (card.damage) text += "Deal " + std::to_string(card.damage) + " damage.";
    if (card.block) text += "Gain " + std::to_string(card.block) + " Block.";
    if (card.vulnerable) text += " Apply " + std::to_string(card.vulnerable) + " Vulnerable.";
    if (card.strength) text += " Gain " + std::to_string(card.strength) + " Strength.";
    if (card.weak) text += " Apply " + std::to_string(card.weak) + " Weak.";
    if (card.exhaust) text += " Exhaust.";
    if (card.model_id == "CARD.SURVIVOR") text += " Discard 1 card.";
    if (card.model_id == "CARD.TRUE_GRIT") text += card.upgraded ? " Exhaust a card from your hand." : " Exhaust a random card from your hand.";
    if (card.model_id == "CARD.ARMAMENTS") text += card.upgraded ? " Upgrade all cards in your hand." : " Upgrade a card in your hand.";
    if (card.model_id == "CARD.HEADBUTT") text += " Put a card from your discard pile on top of your draw pile.";
    if (card.model_id == "CARD.BURNING_PACT") text += card.upgraded ? "Exhaust a card from your hand. Draw 3 cards." : "Exhaust a card from your hand. Draw 2 cards.";
    if (card.model_id == "CARD.PURITY") text += card.upgraded ? "Exhaust up to 5 cards from your hand. Retain. Exhaust." : "Exhaust up to 3 cards from your hand. Retain. Exhaust.";
    return text;
}
std::size_t ActionSpaceView::size() const noexcept {
    if (state_.phase == Phase::card_choice) return state_.selection.candidates.size() + 1;
    if (state_.phase == Phase::play) return state_.zone(Zone::hand).size() + 1;
    if (state_.phase == Phase::discard_choice) return state_.zone(Zone::hand).size();
    return 0;
}
std::optional<ActionView> ActionSpaceView::at(std::size_t index) const noexcept {
    if (index >= size()) return std::nullopt;
    if (state_.phase == Phase::card_choice) {
        const auto& p = state_.selection;
        Input input{state_.boundary, InputKind::confirm_selection, {}, false, p.id};
        if (index < p.candidates.size()) {
            input.card = p.candidates[index];
            input.kind = std::find(p.selected.begin(), p.selected.end(), input.card) == p.selected.end() ? InputKind::select_card : InputKind::deselect_card;
        }
        return ActionView{input, Simulator{}.validate(state_, input)};
    }
    const auto& hand = state_.zone(Zone::hand);
    Input input{state_.boundary, InputKind::end_turn};
    if (index < hand.size()) {
        input.card = hand[index];
        if (state_.phase == Phase::discard_choice) input.kind = InputKind::select_card;
        else {
            input.kind = InputKind::play_card;
            const auto* instance = state_.cards.get(input.card);
            const auto* def = instance ? Ruleset::pinned().card(instance->definition) : nullptr;
            input.target_enemy = def && def->enemy_target;
        }
    }
    return ActionView{input, Simulator{}.validate(state_, input)};
}
} // namespace sts2::engine
