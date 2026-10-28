#include "sts2/observation/observe.hpp"
#include <algorithm>

namespace sts2::engine {
CombatObservationView observe(ConstCombatView s) noexcept {
    if (!valid_data_storage(s.data, s.overflow)) { CombatObservationView invalid{}; invalid.phase = CombatPhase::invalid; return invalid; }
    const auto attack = s.move == BeetleMove::chomp ? 7 : s.move == BeetleMove::stomp ? 13 : 0;
    return {game_build, s.boundary, s.phase, s.player.hp, s.player.max_hp, s.player.block, s.energy,
        s.turn, s.enemy.hp, s.enemy.max_hp, s.enemy.power_amount(PowerId::vulnerable), attack, s.player.has_power(PowerId::shrink), s.move,
        s.zone(Zone::hand), s.zone(Zone::draw).size(), s.zone(Zone::discard).size(), s.zone(Zone::exhaust).size()};
}
std::optional<CardView> observe_card(ConstCombatView s, CardHandle h, const Ruleset& rules) noexcept {
    if (!valid_data_storage(s.data, s.overflow)) return std::nullopt;
    const auto& hand = s.zone(Zone::hand);
    if (std::find(hand.begin(), hand.end(), h) == hand.end()) return std::nullopt;
    return inspect_card(s, h, rules);
}
std::optional<CardView> inspect_card(ConstCombatView s, CardHandle h, const Ruleset& rules) noexcept {
    if (!valid_data_storage(s.data, s.overflow)) return std::nullopt;
    bool visible = false;
    for (const auto zone : {Zone::hand, Zone::draw, Zone::discard, Zone::exhaust}) {
        const auto& cards = s.zone(zone);
        visible = visible || std::find(cards.begin(), cards.end(), h) != cards.end();
    }
    if (!visible) return std::nullopt;
    const auto* instance = s.cards.get(h); if (!instance) return std::nullopt;
    const auto* def = rules.card(instance->definition); if (!def) return std::nullopt;
    CardView view{h, def->model_id, def->name, def->cost, 0, 0, 0, instance->upgraded, def->enemy_target};
    // Native CardModel.UpdateDynamicVarPreview runs global combat hooks only
    // for Hand/Play or explicit combat upgrade-preview copies. inspect_card
    // exposes live pile cards, never upgrade-preview copies or the playing pile.
    const auto& hand = s.zone(Zone::hand);
    const bool active_preview = std::find(hand.begin(), hand.end(), h) != hand.end();
    for (const auto& op : def->program) {
        const auto value = op.base + (instance->upgraded ? op.upgrade : 0);
        if (op.opcode == Opcode::damage) view.damage = static_cast<int>(std::int64_t(value) * (active_preview && s.player.has_power(PowerId::shrink) ? 70 : 100) * (active_preview && s.enemy.power_amount(PowerId::vulnerable) ? 150 : 100) / 10000);
        else if (op.opcode == Opcode::block) view.block = value;
        else if (op.opcode == Opcode::vulnerable) view.vulnerable = value;
    }
    return view;
}
std::string describe(const CardView& card) {
    std::string text;
    if (card.damage) text += "Deal " + std::to_string(card.damage) + " damage.";
    if (card.block) text += "Gain " + std::to_string(card.block) + " Block.";
    if (card.vulnerable) text += " Apply " + std::to_string(card.vulnerable) + " Vulnerable.";
    if (card.model_id == "CARD.SURVIVOR") text += " Discard 1 card.";
    return text;
}
} // namespace sts2::engine
