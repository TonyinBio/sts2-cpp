#include "sts2/engine/combat.hpp"
#include "gameplay.hpp"
#include "sts2/core/sort.hpp"
#include <algorithm>
#include <limits>
#include <new>
#include <stdexcept>

namespace sts2::engine {
using namespace gameplay;
namespace {
constexpr std::array starting_deck = {
    CardSpec{CardId::strike}, CardSpec{CardId::strike}, CardSpec{CardId::strike},
    CardSpec{CardId::strike}, CardSpec{CardId::strike}, CardSpec{CardId::defend},
    CardSpec{CardId::defend}, CardSpec{CardId::defend}, CardSpec{CardId::defend}, CardSpec{CardId::bash}};
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
            if (((id == PowerId::vulnerable || id == PowerId::weak || id == PowerId::artifact) && power.amount <= 0) || (id == PowerId::shrink && power.amount >= 0) ||
                ((id == PowerId::strength || id == PowerId::dexterity) && power.amount == 0) ||
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

}
StepResult initialize_combat(CombatView state, const CombatConfig& config, Scratch& scratch, const Ruleset& rules, Limits limits) {
    if (config.build != game_build || config.ascension != 0 || config.encounter != "ENCOUNTER.SHRINKER_BEETLE_WEAK" ||
        config.seed.empty() || config.seed.size() > 4096 || config.player_max_hp < 1 || config.player_hp < 1 || config.player_hp > config.player_max_hp ||
        config.card_generation == 0 || config.enemy_hp < 0 || config.enemy_hp > 40 || config.exhaust_relics.size() > 2 || config.joss_paper_counter >= 5 ||
        (config.seed.starts_with("old") && std::any_of(config.seed.begin(), config.seed.end(), [](unsigned char c) { return c >= 128; })))
        return {StepStatus::unsupported_content, state.boundary, Restriction::unsupported_setup};
    const auto deck = config.deck.empty() ? std::span<const CardSpec>(starting_deck) : config.deck;
    if (deck.size() > limits.cards) return {StepStatus::resource_limit, state.boundary, Restriction::capacity};
    for (const auto& card : deck) if (!rules.card(card.definition))
        return {StepStatus::unsupported_content, state.boundary, Restriction::unsupported_setup};
    for (std::size_t i = 0; i < config.exhaust_relics.size(); ++i)
        if (static_cast<unsigned>(config.exhaust_relics[i]) > 1 ||
            std::find(config.exhaust_relics.begin(), config.exhaust_relics.begin() + i, config.exhaust_relics[i]) != config.exhaust_relics.begin() + i)
            return {StepStatus::unsupported_content, state.boundary, Restriction::unsupported_setup};
    if (config.joss_paper_counter != 0 && std::find(config.exhaust_relics.begin(), config.exhaust_relics.end(), ExhaustRelic::joss_paper) == config.exhaust_relics.end())
        return {StepStatus::unsupported_content, state.boundary, Restriction::unsupported_setup};
    CombatState next(state.overflow.allocator()); auto next_view = next.view();
    if (!next_view.clock.initialize(config.clock)) return {StepStatus::unsupported_content, state.boundary, Restriction::unsupported_setup};
    try {
        next_view.exhaust_relic_count = static_cast<std::uint8_t>(config.exhaust_relics.size());
        std::copy(config.exhaust_relics.begin(), config.exhaust_relics.end(), next_view.exhaust_relics.begin());
        next_view.joss_paper_counter = config.joss_paper_counter;
        next_view.reserve(deck.size()); next_view.seed = config.seed; next_view.numeric_seed = game_seed(config.seed); next_view.rng = config.initial_rng ? *config.initial_rng : RngBank(config.seed);
        next_view.player = {config.player_hp, config.player_max_hp};
        int enemy_hp = config.enemy_hp;
        if (enemy_hp == 0) (void)next_view.rng.get(RunStream::niche).next_int(38, 41, enemy_hp);
        next_view.enemy = {enemy_hp, enemy_hp}; next_view.energy = 3; next_view.turn = 1; next_view.burning_blood = config.burning_blood;
        for (const auto& card : deck) {
            auto handle = next_view.cards.create({card.definition, card.upgraded, next_view.next_creation_order++, card.origin}, limits.cards, config.card_generation);
            if (!handle.present()) return {StepStatus::resource_limit, state.boundary, Restriction::capacity};
            next_view.zone(Zone::draw).push_back(handle);
        }
        next_view.rng.get(RunStream::shuffle).shuffle<CardHandle>(next_view.zone(Zone::draw));
        emit(next, scratch, CombatEventKind::combat_started);
        draw(next, scratch, 5, rules); next_view.phase = CombatPhase::play; next_view.boundary.value = 1;
        emit(next, scratch, CombatEventKind::combat_updated);
    } catch (const std::bad_alloc&) { return {StepStatus::resource_limit, state.boundary, Restriction::capacity}; } catch (const std::length_error&) { return {StepStatus::resource_limit, state.boundary, Restriction::capacity}; }
    state.replace(std::move(next)); scratch.steps = 0;
    return result(state);
}
std::int32_t effective_card_cost(ConstCombatView state, CardHandle handle, const Ruleset& rules) noexcept {
    const auto* card = state.cards.get(handle);
    if (!card) return 0;
    const auto* definition = rules.card(card->definition);
    if (!definition) return 0;
    std::int64_t cost = definition->cost;
    for (const auto& modifier : state.cost_modifiers) {
        if (modifier.card != handle) continue;
        const auto previous = cost;
        cost = modifier.kind == CostModifierKind::absolute ? modifier.amount : cost + modifier.amount;
        cost = std::clamp<std::int64_t>(cost, std::numeric_limits<std::int32_t>::min(), std::numeric_limits<std::int32_t>::max());
        if (modifier.reduce_only) cost = std::min(cost, previous);
    }
    return static_cast<std::int32_t>(std::max<std::int64_t>(0, cost));
}
Restriction validate_combat(ConstCombatView state, const CombatInput& input, const Ruleset& rules) noexcept {
    if (!state.valid_storage() || !valid_powers(state) || !valid_card_extra_state(state) || !valid_continuations(state, rules) ||
        state.phase == CombatPhase::uninitialized || state.phase == CombatPhase::invalid || state.rules != rules_revision)
        return Restriction::invalid_state;
    if (input.expected_boundary != state.boundary) return Restriction::stale_boundary;
    if (state.phase == CombatPhase::combat_won || state.phase == CombatPhase::player_died) return Restriction::wrong_phase;
    if (state.phase == CombatPhase::card_choice) {
        if (input.prompt != state.selection.id) return Restriction::invalid_selection;
        if (input.target_enemy) return Restriction::malformed_action;
        if (input.kind == CombatInputKind::confirm_selection) {
            if (input.card != CardHandle{}) return Restriction::malformed_action;
            if (state.selection.automatic_at_maximum) return Restriction::wrong_phase;
            return state.selection_order.size() >= state.selection.minimum && state.selection_order.size() <= state.selection.maximum ? Restriction::none : Restriction::invalid_selection;
        }
        if (input.kind != CombatInputKind::select_card && input.kind != CombatInputKind::deselect_card) return Restriction::wrong_phase;
        const bool selected = std::find(state.selection_order.begin(), state.selection_order.end(), input.card) != state.selection_order.end();
        if (input.kind == CombatInputKind::deselect_card) return selected ? Restriction::none : Restriction::invalid_selection;
        return !selected && std::find(state.selection_candidates.begin(), state.selection_candidates.end(), input.card) != state.selection_candidates.end() ? Restriction::none : Restriction::invalid_selection;
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
StepResult advance_combat(CombatView state, Scratch& scratch, const Ruleset& rules) noexcept try {
    if (!state.valid_storage() || !valid_powers(state) || !valid_card_extra_state(state)) return fail(state, Restriction::invalid_state);
    if (state.phase != CombatPhase::play || state.execution.empty()) return result(state);
    while (!state.execution.empty()) {
        if (!consume(scratch)) return fail(state, Restriction::execution_budget);
        const auto step = std::holds_alternative<std::monostate>(state.execution.back().pending) ?
            execute_instruction(state, scratch, rules) : resume_continuation(state, scratch, rules);
        if (step.status == StepStatus::engine_error || step.status == StepStatus::resource_limit || state.phase != CombatPhase::play) return step;
    }
    if (state.enemy.hp == 0 && state.phase == CombatPhase::play) end_combat(state, scratch, true);
    return result(state);
} catch (const std::bad_alloc&) { state.phase = CombatPhase::invalid; return {StepStatus::resource_limit, state.boundary, Restriction::capacity}; } catch (const std::length_error&) { state.phase = CombatPhase::invalid; return {StepStatus::resource_limit, state.boundary, Restriction::capacity}; }
catch (...) { return fail(state, Restriction::invalid_state); }
StepResult apply_combat(CombatView state, const CombatInput& input, Scratch& scratch, const Ruleset& rules) noexcept try {
    const auto restriction = validate_combat(state, input, rules);
    if (restriction != Restriction::none) return result(state, restriction);
    if (state.boundary.value == std::numeric_limits<std::uint32_t>::max()) return fail(state, Restriction::capacity);
    if (!state.clock.advance_action()) return fail(state, Restriction::clock_exhausted);
    scratch.steps = 0;
    if (state.phase == CombatPhase::card_choice) {
        auto selected = state.selection_order;
        if (input.kind == CombatInputKind::select_card) {
            if (selected.size() == state.selection.maximum) selected.pop_back();
            selected.push_back(input.card);
        } else if (input.kind == CombatInputKind::deselect_card) {
            selected.erase(std::find(selected.begin(), selected.end(), input.card));
        }
        if (input.kind == CombatInputKind::confirm_selection || (state.selection.automatic_at_maximum && selected.size() == state.selection.maximum)) {
            EffectFrame resolution = state.execution.back();
            const auto begin = static_cast<std::uint32_t>(state.continuation_cards.size());
            for (auto card : selected) state.continuation_cards.push_back(card);
            const auto* hook = std::get_if<ShuffleContinuation>(&state.execution.back().pending);
            resolution.pending = SelectionContinuation{state.selection.effect, state.selection.zone, begin, static_cast<std::uint32_t>(selected.size()), 0, hook ? hook->remember_to : CardHandle{}};
            if (auto* hook = std::get_if<ShuffleContinuation>(&state.execution.back().pending)) { hook->waiting = false; hook->remember_to = {}; }
            clear_selection(state); state.phase = CombatPhase::play;
            state.execution.push_back(resolution);
            const auto advanced = advance_combat(state, scratch, rules);
            if (advanced.status == StepStatus::engine_error || advanced.status == StepStatus::resource_limit) return advanced;
        }
    } else if (input.kind == CombatInputKind::end_turn) {
        if (!consume(scratch)) return fail(state, Restriction::execution_budget);
        auto hand = state.zone(Zone::hand); auto discard = state.zone(Zone::discard);
        for (auto card : hand) if (!rules.card(state.cards.get(card)->definition)->retain) discard.push_back(card);
        hand.erase(std::remove_if(hand.begin(), hand.end(), [&](auto card) { return !rules.card(state.cards.get(card)->definition)->retain; }), hand.end());
        state.enemy.block = 0;
        snapshot_power_turn(state, scratch, state.enemy, true);
        execute_enemy_move(state, scratch);
        if (state.player.hp == 0) end_combat(state, scratch, false);
        else {
            after_enemy_turn(state, scratch);
            snapshot_power_turn(state, scratch, state.player, false);
            state.counters.started_this_turn = state.counters.attacks_this_turn = 0;
            state.counters.zero_energy_attacks_this_turn = state.counters.non_hand_draws_this_turn = 0;
            state.player.block = 0;
            if (state.turn == std::numeric_limits<int>::max()) return fail(state, Restriction::capacity);
            for (auto it = state.cost_modifiers.begin(); it != state.cost_modifiers.end();) {
                if ((static_cast<unsigned>(it->expiry) & 2U) != 0) it = state.cost_modifiers.erase(it);
                else ++it;
            }
            ++state.turn; refresh_energy(state, 3); draw(state, scratch, 5, rules);
        }
    } else if (input.kind == CombatInputKind::play_card) {
        const auto* card = state.cards.get(input.card);
        const auto cost = effective_card_cost(state, input.card, rules);
        spend_energy(state, cost);
        move_card(state, Zone::hand, Zone::play, input.card);
        if (state.counters.started_plays == UINT32_MAX) return fail(state, Restriction::capacity);
        const auto play_id = state.counters.started_plays;
        state.execution.push_back({input.card, 0, play_id, cost, state.enemy_handle});
        if (cost > 0) emit(state, scratch, CombatEventKind::energy_spent, false, false, {}, 0, cost);
        ++state.counters.started_plays; ++state.counters.started_this_turn;
        if (rules.card(card->definition)->kind == CardKind::attack) {
            ++state.counters.attacks_this_turn;
            if (cost == 0) ++state.counters.zero_energy_attacks_this_turn;
        }
        emit(state, scratch, CombatEventKind::play_started, false, false, input.card, play_id);
        const auto advanced = advance_combat(state, scratch, rules);
        if (advanced.status == StepStatus::engine_error || advanced.status == StepStatus::resource_limit) return advanced;
    } else {
        move_card(state, Zone::hand, Zone::discard, input.card);
        emit(state, scratch, CombatEventKind::discarded, false, false, input.card);
        state.phase = CombatPhase::play;
        const auto advanced = advance_combat(state, scratch, rules);
        if (advanced.status == StepStatus::engine_error || advanced.status == StepStatus::resource_limit) return advanced;
    }
    if (state.phase == CombatPhase::play && !state.execution.empty()) {
        const auto advanced = advance_combat(state, scratch, rules);
        if (advanced.status == StepStatus::engine_error || advanced.status == StepStatus::resource_limit) return advanced;
    }
    if (state.phase == CombatPhase::invalid) return {StepStatus::engine_error, state.boundary, Restriction::execution_budget};
    ++state.boundary.value;
    emit(state, scratch, CombatEventKind::combat_updated);
    return result(state);
} catch (const std::bad_alloc&) {
    state.phase = CombatPhase::invalid;
    return {StepStatus::resource_limit, state.boundary, Restriction::capacity};
} catch (const std::length_error&) {
    state.phase = CombatPhase::invalid;
    return {StepStatus::resource_limit, state.boundary, Restriction::capacity};
} catch (...) { return fail(state, Restriction::invalid_state); }
StepResult apply_power_scenario(CombatView state, const PowerApplication& operation, Scratch& scratch) noexcept {
    if (!valid_combat(state)) return result(state, Restriction::invalid_state);
    if (state.phase != CombatPhase::play || !state.execution.empty()) return result(state, Restriction::wrong_phase);
    if (operation.definition >= PowerId::count || operation.amount < -999999999 || operation.amount > 999999999 ||
        (operation.source.present() && !state.cards.get(operation.source))) return result(state, Restriction::malformed_action);
    try {
        (void)apply_power(state, scratch, operation.definition, operation.amount, operation.enemy_owner, false,
            operation.source, operation.has_applier, operation.applier_enemy);
        return result(state);
    } catch (...) { return fail(state, Restriction::capacity); }
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
    if (!valid_powers(state) || !valid_card_extra_state(state)) return false;
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
} // namespace sts2::engine
