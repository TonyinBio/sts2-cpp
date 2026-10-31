#include "sts2/engine/combat.hpp"
#include "validation.hpp"
#include "../execution/interpreter.hpp"
#include "../execution/selection.hpp"
#include "../mechanics/cards.hpp"
#include "../content/powers.hpp"
#include <algorithm>

namespace sts2::engine {
using namespace gameplay;
bool valid_powers(ConstCombatView state) noexcept {
    const bool canonical = state.creature_pool && state.creature_pool_owner == &state.creature_overflow;
    const auto for_each_creature = [&](auto visit) {
        if (canonical) {
            const auto slots = state.creature_pool->slots.values(state.creature_overflow);
            for (std::size_t i = 0; i < slots.size(); ++i) if (slots[i].live)
                visit(&slots[i].value, CreatureHandle{static_cast<std::uint32_t>(i), slots[i].generation} != state.player_handle);
        } else { visit(&state.player, false); visit(&state.enemy, true); }
    };
    const auto valid_creature = [&](const CreatureState* creature, bool enemy) {
        if (!creature->powers.valid(state.creature_overflow)) return false;
        const auto powers = creature->power_instances(state.creature_overflow);
        for (std::size_t i = 0; i < powers.size(); ++i) {
            const auto& power = powers[i]; const auto id = power.definition;
            if (id >= PowerId::count || !power.active || power.enemy_owner != enemy || power.instance >= state.next_power_instance || power.stacking > PowerInstance::Stacking::separate) return false;
            if (!valid_power_amount(id, power.amount) ||
                (power.source.present() && !state.cards.get(power.source)) || (power.has_applier && (!power.applier.present() || !state.creature(power.applier)))) return false;
            for (std::size_t j = 0; j < i; ++j) {
                const auto& previous = powers[j];
                if (power.instance == previous.instance) return false;
                if (id == previous.definition && power.stacking == previous.stacking &&
                    (power.stacking == PowerInstance::Stacking::by_type || (power.stacking == PowerInstance::Stacking::by_applier && power.applier == previous.applier))) return false;
            }
        }
        return true;
    };
    bool valid = true;
    for_each_creature([&](const CreatureState* creature, bool enemy) { if (!valid_creature(creature, enemy)) valid = false; });
    if (!valid) return false;
    for_each_creature([&](const CreatureState* a, bool) {
        for_each_creature([&](const CreatureState* b, bool) {
            if (a == b) return;
            for (const auto& pa : a->power_instances(state.creature_overflow))
                for (const auto& pb : b->power_instances(state.creature_overflow)) if (pa.instance == pb.instance) valid = false;
        });
    });
    return valid;
}
Restriction validate_combat(ConstCombatView state, const CombatInput& input, const Ruleset& rules) noexcept {
    if (!state.valid_storage() || !valid_powers(state) || !valid_card_extra(state) || !valid_continuations(state, rules) ||
        state.phase == CombatPhase::uninitialized || state.phase == CombatPhase::invalid || state.rules != rules_revision)
        return Restriction::invalid_state;
    if (input.expected_boundary != state.boundary) return Restriction::stale_boundary;
    if (state.phase == CombatPhase::combat_won || state.phase == CombatPhase::player_died) return Restriction::wrong_phase;
    if (state.phase == CombatPhase::card_choice) {
        return validate_selection_input(state, input);
    }
    if (input.prompt != PromptId{}) return Restriction::invalid_selection;
    if (input.kind == CombatInputKind::end_turn) {
        if (input.card != CardHandle{} || input.target_enemy) return Restriction::malformed_action;
        return state.phase == CombatPhase::play ? Restriction::none : Restriction::wrong_phase;
    }
    if (input.kind != CombatInputKind::play_card && input.kind != CombatInputKind::select_card) return Restriction::malformed_action;
    if (input.kind == CombatInputKind::select_card) {
        if (state.phase != CombatPhase::discard_choice) return Restriction::wrong_phase;
        if (input.target_enemy) return Restriction::malformed_action;
    } else if (state.phase != CombatPhase::play) return Restriction::wrong_phase;
    const auto* card = state.cards.get(input.card);
    const auto hand = state.zone(Zone::hand);
    if (!card || std::find(hand.begin(), hand.end(), input.card) == hand.end()) return Restriction::stale_card;
    if (input.kind == CombatInputKind::select_card) return Restriction::none;
    const auto* definition = rules.card(card->definition);
    if (!definition) return Restriction::invalid_state;
    if (definition->enemy_target != input.target_enemy || (input.target_enemy && state.enemy.hp <= 0)) return Restriction::invalid_target;
    if (state.energy < effective_card_cost(state, input.card, rules)) return Restriction::insufficient_energy;
    return Restriction::none;
}
bool valid_combat(ConstCombatView state, const Ruleset& rules) noexcept {
    if (!state.player_handle.present() || !state.enemy_handle.present() || state.player_handle == state.enemy_handle) return false;
    if (!state.valid_storage() || !state.cards.valid()) return false;
    if (state.exhaust_relic_count > state.exhaust_relics.size() ||
        (state.joss_paper_counter >= 5 && std::none_of(state.execution.begin(), state.execution.end(), [](const auto& frame) { return std::holds_alternative<RelicDrawCompletion>(frame.pending); }))) return false;
    bool joss = false;
    for (std::size_t i = 0; i < state.exhaust_relics.size(); ++i) {
        if (i >= state.exhaust_relic_count) { if (state.exhaust_relics[i] != ExhaustRelic{}) return false; continue; }
        const auto relic = state.exhaust_relics[i];
        if (static_cast<unsigned>(relic) > 1 || std::find(state.exhaust_relics.begin(), state.exhaust_relics.begin() + i, relic) != state.exhaust_relics.begin() + i) return false;
        joss |= relic == ExhaustRelic::joss_paper;
    }
    if (!joss && state.joss_paper_counter != 0) return false;
    if (static_cast<unsigned>(state.phase) > static_cast<unsigned>(CombatPhase::invalid) ||
        static_cast<unsigned>(state.move) > static_cast<unsigned>(BeetleMove::stomp) ||
        state.seed.empty() || state.seed.size() > 4096 || state.numeric_seed != game_seed(state.seed) ||
        state.rules != rules_revision || state.phase == CombatPhase::uninitialized || state.phase == CombatPhase::invalid ||
        state.boundary.value == 0 || state.player.max_hp < 1 || state.player.hp < 0 || state.player.hp > state.player.max_hp ||
        state.enemy.max_hp < 1 || state.enemy.hp < 0 || state.enemy.hp > state.enemy.max_hp ||
        state.player.block < 0 || state.enemy.block < 0 ||
        state.energy < 0 || state.turn < 1 || state.zone(Zone::hand).size() > 10) return false;
    if (state.clock.elapsed_seconds() != std::int64_t(state.boundary.value - 1) * state.clock.config().seconds_per_action) return false;
    for (const auto& stream : state.rng.streams) {
        const auto& s = stream.state().words;
        if ((s[0] | s[1] | s[2] | s[3]) == 0) return false;
    }
    const bool terminal = state.phase == CombatPhase::combat_won || state.phase == CombatPhase::player_died;
    if ((state.phase == CombatPhase::combat_won && (state.enemy.hp != 0 || state.player.hp == 0)) ||
        (state.phase == CombatPhase::player_died && state.player.hp != 0) ||
        (!terminal && (state.enemy.hp == 0 || state.player.hp == 0))) return false;
    if (state.next_prompt.value == 0 || (state.phase != CombatPhase::card_choice && (state.selection != CardSelection{} || !state.selection_candidates.empty() || !state.selection_order.empty()))) return false;
    if (!valid_continuations(state, rules)) return false;
    if (state.phase == CombatPhase::combat_won && (!state.player.powers.empty() || !state.enemy.powers.empty())) return false;
    const auto& counters = state.counters;
    if (counters.finished_plays > counters.started_plays || counters.started_this_turn > counters.started_plays ||
        counters.attacks_this_turn > counters.started_this_turn || counters.zero_energy_attacks_this_turn > counters.attacks_this_turn ||
        counters.finished_ethereal > counters.finished_plays) return false;
    if (!valid_powers(state) || !valid_card_extra(state)) return false;
    for (const auto& modifier : state.cost_modifiers)
        if (!state.cards.get(modifier.card) || static_cast<unsigned>(modifier.kind) > 1 || (static_cast<unsigned>(modifier.expiry) & ~6U) != 0) return false;
    std::size_t live = 0, membership = 0;
    for (const auto& slot : state.cards.slots()) {
        if (slot.generation == 0 || !rules.card(slot.value.definition) || slot.value.creation_order >= state.next_creation_order || slot.value.last_finished_player_turn < 0 || slot.value.last_finished_player_turn > state.turn) return false;
        if (!slot.live) continue;
        ++live;
        if (slot.generation == 0 || !rules.card(slot.value.definition) || slot.value.creation_order >= state.next_creation_order || slot.value.last_finished_player_turn < 0 || slot.value.last_finished_player_turn > state.turn) return false;
    }
    const auto zones = state.zone_views();
    for (std::size_t a = 0; a < zones.size(); ++a) {
        const auto& zone = zones[a]; membership += zone.size();
        if (state.phase == CombatPhase::combat_won && !zone.empty()) return false;
        for (std::size_t i = 0; i < zone.size(); ++i) {
            if (!state.cards.get(zone[i])) return false;
            for (std::size_t b = 0; b <= a; ++b) {
                const auto end = b == a ? i : zones[b].size();
                for (std::size_t j = 0; j < end; ++j) if (zone[i] == zones[b][j]) return false;
            }
        }
    }
    if (state.phase == CombatPhase::combat_won) return membership == 0;
    if (membership > live) return false;
    for (std::size_t i = 0; i < state.cards.slots().size(); ++i) {
        const auto& slot = state.cards.slots()[i];
        if (!slot.live) continue;
        const CardHandle card{static_cast<std::uint32_t>(i), slot.generation};
        bool referenced = false;
        for (const auto& zone : zones) if (std::find(zone.begin(), zone.end(), card) != zone.end()) referenced = true;
        for (const auto& frame : state.execution) if (frame.source == card) referenced = true;
        if (!referenced && !slot.value.removed) return false;
    }
    return true;
}
}
