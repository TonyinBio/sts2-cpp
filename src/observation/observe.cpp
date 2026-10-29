#include "sts2/observation/observe.hpp"
#include "../engine/gameplay.hpp"
#include <algorithm>

namespace sts2::engine {
CombatObservationView observe(ConstCombatView s) noexcept {
    if (!valid_data_storage(s.data, s.overflow)) { CombatObservationView invalid{}; invalid.phase = CombatPhase::invalid; return invalid; }
    const auto intent = gameplay::visible_enemy_intent(s);
    CombatObservationView view{game_build, s.boundary, s.phase, s.player.hp, s.player.max_hp, s.player.block, s.energy,
        s.turn, s.enemy.hp, s.enemy.max_hp, s.enemy.power_amount(PowerId::vulnerable), intent.attack, s.player.has_power(PowerId::shrink), intent.move,
        s.zone(Zone::hand), s.zone(Zone::draw).size(), s.zone(Zone::discard).size(), s.zone(Zone::exhaust).size(), std::nullopt, s.player.power_amount(PowerId::strength), s.player.power_amount(PowerId::dexterity), s.player.power_amount(PowerId::weak), s.enemy.power_amount(PowerId::weak)};
    if (s.phase == CombatPhase::card_choice) view.selection = CardSelectionView{s.selection.id, s.selection.effect, s.selection_candidates, s.selection_order, s.selection.minimum, s.selection.maximum, s.selection.automatic_at_maximum, !s.selection.automatic_at_maximum && s.selection_order.size() >= s.selection.minimum};
    return view;
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
    CardView view{h, def->model_id, def->name, effective_card_cost(s, h, rules), 0, 0, 0, instance->upgraded, def->enemy_target, def->rarity};
    // Native CardModel.UpdateDynamicVarPreview runs global combat hooks only
    // for Hand/Play or explicit combat upgrade-preview copies. inspect_card
    // exposes live pile cards, never upgrade-preview copies or the playing pile.
    const auto& hand = s.zone(Zone::hand);
    const bool active_preview = std::find(hand.begin(), hand.end(), h) != hand.end();
    for (const auto& op : def->program) {
        const auto value = op.base + (instance->upgraded ? op.upgrade : 0);
        if (op.opcode == Opcode::damage) view.damage = static_cast<int>(std::max<std::int64_t>(0, std::int64_t(value) + (active_preview ? s.player.power_amount(PowerId::strength) : 0)) * (active_preview && s.player.has_power(PowerId::shrink) ? 70 : 100) * (active_preview && s.player.power_amount(PowerId::weak) > 0 ? 75 : 100) * (active_preview && s.enemy.power_amount(PowerId::vulnerable) ? 150 : 100) / 1000000);
        else if (op.opcode == Opcode::block) view.block = static_cast<int>(std::max<std::int64_t>(0, std::int64_t(value) + (active_preview ? s.player.power_amount(PowerId::dexterity) : 0)));
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
    if (card.model_id == "CARD.PURITY") text += std::string("Retain. Exhaust up to ") + (card.upgraded ? "5" : "3") + " cards in your hand. Exhaust.";
    if (card.model_id == "CARD.ARMAMENTS") text += card.upgraded ? " Upgrade all cards in your hand." : " Upgrade a card in your hand.";
    if (card.model_id == "CARD.HEADBUTT") text += " Put a card from your discard pile on top of your draw pile.";
    if (card.model_id == "CARD.BURNING_PACT") text += std::string("Exhaust 1 card. Draw ") + (card.upgraded ? "3" : "2") + " cards.";
    if (card.model_id == "CARD.TRUE_GRIT") text += card.upgraded ? " Exhaust a card from your hand." : " Exhaust a random card from your hand.";
    return text;
}
} // namespace sts2::engine
