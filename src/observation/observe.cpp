#include "sts2/observation/observe.hpp"
#include <algorithm>

namespace sts2::engine {
ObservationView observe(const State& s) noexcept {
    const auto base_attack = s.move == BeetleMove::chomp ? 7 : s.move == BeetleMove::stomp ? 13 : 0;
    const auto attack = std::max(0, base_attack + s.enemy.strength) * (s.enemy.weak > 0 ? 75 : 100) / 100;
    return {game_build, s.boundary, s.phase, s.player.hp, s.player.max_hp, s.player.block, s.energy,
        s.turn, s.enemy.hp, s.enemy.max_hp, s.enemy.vulnerable, attack, s.player.shrink, s.move,
        s.zone(Zone::hand), s.zone(Zone::draw).size(), s.zone(Zone::discard).size(), s.zone(Zone::exhaust).size(), s.player.strength, s.player.dexterity, s.player.weak, s.enemy.weak};
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
    const bool active_preview = std::find(s.zone(Zone::hand).begin(), s.zone(Zone::hand).end(), h) != s.zone(Zone::hand).end();
    CardView view{h, def->model_id, def->name, def->cost, 0, 0, 0, instance->upgraded, def->enemy_target};
    for (const auto& op : def->program) {
        const auto value = op.base + (instance->upgraded ? op.upgrade : 0);
        if (op.opcode == Opcode::damage) view.damage = active_preview ? static_cast<int>(std::int64_t(std::max(0, value + s.player.strength)) * (s.player.shrink ? 70 : 100) * (s.player.weak > 0 ? 75 : 100) * (s.enemy.vulnerable ? 150 : 100) / 1000000) : value;
        else if (op.opcode == Opcode::block) view.block = active_preview ? std::max(0, value + s.player.dexterity) : value;
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
    return text;
}
std::size_t ActionSpaceView::size() const noexcept {
    if (state_.power_dispatch.active || state_.card_played_dispatch.stage) return 0;
    if (state_.phase == Phase::play) return state_.zone(Zone::hand).size() + 1;
    if (state_.phase == Phase::discard_choice) return state_.zone(Zone::hand).size();
    return 0;
}
std::optional<ActionView> ActionSpaceView::at(std::size_t index) const noexcept {
    if (index >= size()) return std::nullopt;
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
