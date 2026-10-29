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
std::uint32_t apply_power(CombatView s, Scratch& scratch, PowerId id, int amount, bool enemy, bool enemy_side, CardHandle source, bool has_applier, bool applier_enemy, PowerInstance::Stacking stacking, CreatureHandle applier, CreatureHandle target, std::uint32_t existing_instance) {
    auto* recipient = target.present() ? s.creature(target) : (enemy ? &s.enemy : &s.player);
    if (!recipient) throw std::invalid_argument("Stale power target");
    auto& creature = *recipient;
    if (!has_applier) applier = {};
    if (has_applier && !applier.present()) applier = applier_enemy ? s.enemy_handle : s.player_handle;
    auto powers = creature.power_instances(s.creature_overflow);
    PowerInstance* power = existing_instance == UINT32_MAX ? nullptr : creature.find_power_instance(existing_instance, s.creature_overflow);
    if (existing_instance != UINT32_MAX && (!power || power->definition != id)) throw std::invalid_argument("Stale power instance");
    if (!power) for (auto& candidate : powers) if (candidate.definition == id && candidate.stacking == stacking &&
        (stacking == PowerInstance::Stacking::by_type || (stacking == PowerInstance::Stacking::by_applier && candidate.applier == applier))) { power = &candidate; break; }
    const auto modifier = before_power_application(creature, s.creature_overflow, id, amount);
    amount = modifier.amount;
    if (!power && amount == 0) { after_power_application(s, scratch, creature, modifier, enemy, enemy_side, target); return UINT32_MAX; }
    if (!power) {
        if (s.next_power_instance == UINT32_MAX) throw std::length_error("power identity exhausted");
        PowerInstance next{s.next_power_instance++, id, amount, 0, enemy, true, !enemy && (id == PowerId::weak || id == PowerId::vulnerable || id == PowerId::shrink)};
        next.source = source; next.applier_enemy = applier_enemy; next.has_applier = has_applier; next.applier = applier; next.stacking = stacking;
        powers.push_back(next); power = &powers.back();
    } else {
        const auto next = std::int64_t(power->amount) + amount;
        if (next < INT32_MIN || next > INT32_MAX) throw std::length_error("power amount exhausted");
        power->amount = static_cast<std::int32_t>(std::clamp<std::int64_t>(next, -999999999, 999999999));
    }
    const auto identity = power->instance;
    emit(s, scratch, CombatEventKind::power_received, enemy_side, enemy, {}, identity, amount, {}, has_applier, applier_enemy);
    // Artifact callbacks can mutate the sequence: reacquire by stable instance.
    after_power_application(s, scratch, creature, modifier, enemy, enemy_side, target);
    power = creature.find_power_instance(identity, s.creature_overflow);
    if (power && ((id == PowerId::strength || id == PowerId::dexterity || id == PowerId::shrink) ? power->amount == 0 : power->amount <= 0)) {
        power->active = false;
        emit(s, scratch, CombatEventKind::power_changed, enemy_side, enemy, {}, identity);
        powers.erase(std::find_if(powers.begin(), powers.end(), [&](const auto& p) { return p.instance == identity; }));
    }
    return identity;
}
void expire_power(CombatView state, Scratch& scratch, CreatureState& creature, std::uint32_t identity) {
    auto* power = creature.find_power_instance(identity, state.creature_overflow);
    if (!power || power->amount <= 0) return;
    if (power->skip_next_tick) { power->skip_next_tick = false; return; }
    --power->amount; power->active = power->amount != 0;
    emit(state, scratch, CombatEventKind::power_received, true, power->enemy_owner, {}, power->instance, -1, {}, false);
    if (power->amount == 0) {
        auto powers = creature.power_instances(state.creature_overflow);
        powers.erase(std::find_if(powers.begin(), powers.end(), [&](const auto& p) { return p.instance == identity; }));
    }
}
void snapshot_power_turn(CombatView state, Scratch& scratch, CreatureState& creature, bool enemy) {
    for (auto& power : creature.power_instances(state.creature_overflow)) {
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
HitResult damage(CreatureState& target, int base, const CreatureState& source, const OverflowStorage& owner) noexcept {
    return receive_damage(target, modify_attack_damage(target, base, source, owner));
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
    const auto value = modify_block(state.player, state.creature_overflow, amount);
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
void remember_card_choice(CombatView state, CardHandle receiver, CardHandle choice) {
    auto* card = state.cards.get(receiver);
    if (!card || !state.cards.get(choice)) throw std::invalid_argument("Stale remembered choice");
    card->extra = RememberedCardChoice{choice};
}
bool valid_card_extra_state(ConstCombatView state) noexcept {
    for (const auto& slot : state.cards.slots()) {
        if (slot.value.extra.index() > 1) return false;
        if (!slot.live) continue;
        if (const auto* memory = std::get_if<RememberedCardChoice>(&slot.value.extra))
            if (!state.cards.get(memory->card)) return false;
    }
    return true;
}
}
