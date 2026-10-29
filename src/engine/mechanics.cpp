#include "gameplay.hpp"
#include "sts2/core/sort.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace sts2::engine::gameplay {
void emit(CombatView s, Scratch& scratch, CombatEventKind kind, bool enemy_side, bool actor_enemy,
             CardHandle card, std::uint32_t object, int amount, HitResult hit, bool has_applier, bool applier_enemy) {
    if (!scratch.trace.receive) return;
    CombatEvent event{kind, s.turn, enemy_side, actor_enemy, card, object, amount, hit};
    event.has_applier = has_applier;
    event.applier_enemy = applier_enemy;
    scratch.trace.emit(s, event);
}
std::uint32_t apply_power(CombatView s, Scratch& scratch, PowerId id, int amount, bool enemy, bool enemy_side, CardHandle source, bool has_applier, bool applier_enemy) {
    auto& creature = enemy ? s.enemy : s.player;
    auto& power = creature.power(id);
    const auto modifier = before_power_application(creature, id, amount);
    amount = modifier.amount;
    auto after_modifier = [&] { after_power_application(s, scratch, creature, modifier, enemy, enemy_side); };
    if (!power.active && amount == 0) { after_modifier(); return UINT32_MAX; }
    if (!power.active) {
        if (s.next_power_instance == UINT32_MAX) throw std::length_error("power identity exhausted");
        power = {s.next_power_instance++, id, amount, 0, enemy, true, !enemy && (id == PowerId::weak || id == PowerId::vulnerable || id == PowerId::shrink)};
        power.source = source; power.applier_enemy = applier_enemy; power.has_applier = has_applier;
        creature.power_order[creature.power_count++] = id;
    } else {
        const auto next = std::int64_t(power.amount) + amount;
        if (next < INT32_MIN || next > INT32_MAX) throw std::length_error("power amount exhausted");
        power.amount = static_cast<std::int32_t>(std::clamp<std::int64_t>(next, -999999999, 999999999));
    }
    emit(s, scratch, CombatEventKind::power_received, enemy_side, enemy, {}, power.instance, amount, {}, has_applier, applier_enemy);
    const auto identity = power.instance;
    after_modifier();
    if ((id == PowerId::strength || id == PowerId::dexterity || id == PowerId::shrink) ? power.amount == 0 : power.amount <= 0) {
        power.active = false;
        emit(s, scratch, CombatEventKind::power_changed, enemy_side, enemy, {}, identity);
        const auto end = creature.power_order.begin() + creature.power_count;
        const auto at = std::find(creature.power_order.begin(), end, id);
        std::move(at + 1, end, at); creature.power_order[--creature.power_count] = PowerId{};
        power = {};
    }
    return identity;
}
void expire_power(CombatView state, Scratch& scratch, CreatureState& creature, PowerId id) {
    auto& power = creature.power(id);
    if (!power.active || power.amount <= 0) return;
    if (power.skip_next_tick) { power.skip_next_tick = false; return; }
    --power.amount;
    power.active = power.amount != 0;
    // Publish the final identity/value before reclaiming its slot.
    emit(state, scratch, CombatEventKind::power_received, true, power.enemy_owner, {}, power.instance, -1, {}, false);
    if (!power.active) {
        const auto end = creature.power_order.begin() + creature.power_count;
        const auto at = std::find(creature.power_order.begin(), end, id);
        std::move(at + 1, end, at);
        creature.power_order[--creature.power_count] = PowerId{};
        power = {};
    }
}
void snapshot_power_turn(CombatView state, Scratch& scratch, CreatureState& creature, bool enemy) {
    for (auto id : creature.power_ids()) {
        auto& power = creature.power(id);
        power.amount_on_turn_start = power.amount;
        emit(state, scratch, CombatEventKind::power_changed, true, enemy, {}, power.instance);
    }
}
void move_card(CombatView state, Zone from, Zone to, CardHandle card) {
    auto source = state.zone(from);
    source.erase(std::find(source.begin(), source.end(), card));
    state.zone(to).push_back(card);
}
void reshuffle(CombatView state, const Ruleset& rules) {
    auto pile = state.zone(Zone::draw); auto discard = state.zone(Zone::discard);
    pile.insert(pile.end(), discard.begin(), discard.end()); discard.clear();
    game_sort<CardHandle>(pile, [&](CardHandle a, CardHandle b) {
        const auto& ca = *state.cards.get(a); const auto& cb = *state.cards.get(b);
        const auto ida = rules.card(ca.definition)->model_id, idb = rules.card(cb.definition)->model_id;
        return ida == idb ? ca.upgraded < cb.upgraded : ida < idb;
    });
    state.rng.get(RunStream::shuffle).shuffle<CardHandle>(pile);
}
void draw_one(CombatView state, Scratch& scratch, bool from_hand_draw) {
    auto pile = state.zone(Zone::draw); auto hand = state.zone(Zone::hand);
    auto card = pile.front(); pile.erase(pile.begin()); hand.push_back(card);
    if (!from_hand_draw) {
        if (state.counters.non_hand_draws_this_turn == UINT32_MAX) throw std::length_error("draw count exhausted");
        ++state.counters.non_hand_draws_this_turn;
    }
    if (scratch.trace.receive) {
        CombatEvent event{CombatEventKind::drawn, state.turn, false, false, card};
        event.from_hand_draw = from_hand_draw; scratch.trace.emit(state, event);
    }
}
void draw(CombatView state, Scratch& scratch, int count, const Ruleset& rules, bool from_hand_draw) {
    if (state.enemy.hp == 0 || state.player.hp == 0 || count <= 0) return;
    if (!state.shuffle_order.empty()) {
        EffectFrame frame;
        if (!state.execution.empty()) frame = state.execution.back();
        frame.pending = DrawContinuation{count, from_hand_draw};
        state.execution.push_back(frame);
        return;
    }
    while (count-- > 0 && state.zone(Zone::hand).size() < 10) {
        if (state.zone(Zone::draw).empty() && !state.zone(Zone::discard).empty()) reshuffle(state, rules);
        if (state.zone(Zone::draw).empty()) break;
        draw_one(state, scratch, from_hand_draw);
    }
}
HitResult damage(CreatureState& target, int base, const CreatureState& source) noexcept {
    return receive_damage(target, modify_attack_damage(target, base, source));
}
HitResult receive_damage(CreatureState& target, int amount) noexcept {
    const auto blocked = std::min(target.block, amount);
    const auto unblocked = std::min(target.hp, amount - blocked);
    const HitResult hit{blocked, unblocked, amount - blocked - unblocked,
        target.block > 0 && blocked == target.block, blocked >= amount, unblocked == target.hp};
    target.block -= blocked; target.hp -= unblocked;
    if (unblocked > 0) ++target.positive_unblocked_hits;
    return hit;
}
void exhaust(CombatView state, Scratch& scratch, Zone from, CardHandle card) {
    if (state.enemy.hp == 0 || state.player.hp == 0) return;
    move_card(state, from, Zone::exhaust, card);
    emit(state, scratch, CombatEventKind::exhausted, false, false, card);
    after_card_exhausted(state, scratch, card);
}
bool gain_block(CombatView state, Scratch& scratch, int amount, CardHandle source, std::uint32_t play) {
    const auto value = modify_block(state.player, amount);
    if (value > std::numeric_limits<int>::max() - state.player.block) return false;
    state.player.block += static_cast<int>(value);
    emit(state, scratch, CombatEventKind::block_gained, false, false, source, play, static_cast<int>(value));
    return true;
}
int heal(CreatureState& creature, int amount) noexcept {
    const auto healed = std::min(std::max(0, amount), creature.max_hp - creature.hp);
    creature.hp += healed; return healed;
}
void spend_energy(CombatView state, int amount) noexcept { state.energy -= amount; }
void refresh_energy(CombatView state, int amount) noexcept { state.energy = amount; }
}
