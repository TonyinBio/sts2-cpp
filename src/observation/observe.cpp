#include "sts2/observation/observe.hpp"
#include <algorithm>

namespace sts2::engine {
ObservationView observe(const State& s) noexcept {
    const auto attack = s.move == BeetleMove::chomp ? 7 : s.move == BeetleMove::stomp ? 13 : 0;
    return {game_build, s.boundary, s.phase, s.player.hp, s.player.max_hp, s.player.block, s.energy,
        s.turn, s.enemy.hp, s.enemy.max_hp, s.enemy.vulnerable, attack, s.player.shrink, s.move,
        s.zone(Zone::hand), s.zone(Zone::draw).size(), s.zone(Zone::discard).size(), s.zone(Zone::exhaust).size()};
}
std::optional<CardView> observe_card(const State& s, CardHandle h, const Ruleset& rules) noexcept {
    const auto& hand = s.zone(Zone::hand);
    if (std::find(hand.begin(), hand.end(), h) == hand.end()) return std::nullopt;
    const auto* instance = s.cards.get(h); if (!instance) return std::nullopt;
    const auto* def = rules.card(instance->definition); if (!def) return std::nullopt;
    CardView view{h, def->model_id, def->name, def->cost, 0, 0, 0, instance->upgraded, def->enemy_target};
    for (const auto& op : def->program) {
        const auto value = op.base + (instance->upgraded ? op.upgrade : 0);
        if (op.opcode == Opcode::damage) view.damage = static_cast<int>(std::int64_t(value) * (s.player.shrink ? 70 : 100) * (s.enemy.vulnerable ? 150 : 100) / 10000);
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
std::size_t ActionSpaceView::size() const noexcept {
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
