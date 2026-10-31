#include "powers.hpp"
#include "../content/powers.hpp"
#include "../combat/trace.hpp"
#include <algorithm>
#include <stdexcept>

namespace sts2::engine::gameplay {
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
        PowerInstance next{s.next_power_instance++, id, amount, 0, enemy, true, skip_initial_power_tick(id, enemy)};
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
    if (power && power_is_depleted(id, power->amount)) {
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
}
