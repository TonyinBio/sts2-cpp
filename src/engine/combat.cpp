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
    for (const auto* creature : {&state.player, &state.enemy}) {
        if (creature->power_count > CreatureState::power_type_count) return false;
        std::array<bool, CreatureState::power_type_count> seen {};
        for (auto id : creature->power_ids()) {
            const auto slot = static_cast<std::size_t>(id);
            if (slot >= seen.size() || seen[slot]) return false;
            seen[slot] = true;
            const auto& p = creature->power(id);
            if (p.definition != id || !p.active || p.enemy_owner != (creature == &state.enemy) || p.instance >= state.next_power_instance) return false;
            if (((id == PowerId::vulnerable || id == PowerId::weak || id == PowerId::artifact) && p.amount <= 0) || (id == PowerId::shrink && p.amount >= 0) ||
                ((id == PowerId::strength || id == PowerId::dexterity) && p.amount == 0) ||
                (p.source.present() && !state.cards.get(p.source))) return false;
        }
        for (std::size_t i = creature->power_count; i < creature->power_order.size(); ++i)
            if (creature->power_order[i] != PowerId{}) return false;
        for (std::size_t i = 0; i < seen.size(); ++i) {
            if (!seen[i] && creature->powers[i] != PowerInstance{}) return false;
            for (std::size_t j = 0; seen[i] && j < i; ++j)
                if (seen[j] && creature->powers[i].instance == creature->powers[j].instance) return false;
        }
    }
    for (auto player : state.player.power_ids())
        for (auto enemy : state.enemy.power_ids())
            if (state.player.power(player).instance == state.enemy.power(enemy).instance) return false;
    return true;
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
    if (!valid_data_storage(state.data, state.overflow) || !valid_powers(state) ||
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
    if (!valid_data_storage(state.data, state.overflow) || !valid_powers(state)) return fail(state, Restriction::invalid_state);
    if (state.phase != CombatPhase::play || state.execution.empty()) return result(state);
    while (!state.execution.empty()) {
        if (!consume(scratch)) return fail(state, Restriction::execution_budget);
        const auto step = execute_instruction(state, scratch, rules);
        if (step.status == StepStatus::engine_error || step.status == StepStatus::resource_limit || state.phase != CombatPhase::play) return step;
    }
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
            for (auto card : selected) {
                if (state.selection.effect == SelectionEffect::upgrade) state.cards.get(card)->upgraded = true;
                else if (state.selection.effect == SelectionEffect::topdeck) {
                    move_card(state, state.selection.zone, Zone::draw, card);
                    auto pile = state.zone(Zone::draw); std::rotate(pile.begin(), pile.end() - 1, pile.end());
                } else exhaust(state, scratch, state.selection.zone, card);
            }
            clear_selection(state); state.phase = CombatPhase::play;
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
        switch (state.move) {
        case BeetleMove::shrink: (void)apply_power(state, scratch, PowerId::shrink, -1, false, true, {}, true, true); state.move = BeetleMove::chomp; break;
        case BeetleMove::chomp: case BeetleMove::stomp: {
            const auto hit = damage(state.player, state.move == BeetleMove::chomp ? 7 : 13, state.enemy);
            emit(state, scratch, CombatEventKind::damaged, true, false, {}, 0, 0, hit);
            emit(state, scratch, CombatEventKind::attacked, true, true, {}, 0, 0, hit);
            if (state.player.hp > 0) state.move = state.move == BeetleMove::chomp ? BeetleMove::stomp : BeetleMove::chomp;
            break;
        }
        }
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
        state.execution.push_back({input.card, 0, play_id, cost, input.target_enemy});
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
    if (!valid_data_storage(state.data, state.overflow) || !state.cards.valid()) return false;
    if (state.exhaust_relic_count > state.exhaust_relics.size() || state.joss_paper_counter >= 5 || state.exhaust_dispatch != ExhaustDispatch{}) return false;
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
    if (state.phase == CombatPhase::card_choice) {
        const auto& p = state.selection;
        if (state.execution.size() != 1 || state.zone(Zone::play).size() != 1 ||
            p.id.value == 0 || p.id.value >= state.next_prompt.value || p.minimum > p.maximum ||
            p.maximum == 0 || state.selection_order.size() > p.maximum || static_cast<unsigned>(p.zone) >= static_cast<unsigned>(Zone::count)) return false;
        const auto& frame = state.execution.back(); const auto* source = state.cards.get(frame.source);
        if (!source || state.zone(Zone::play).front() != frame.source) return false;
        const auto* definition = rules.card(source->definition);
        if (!definition || frame.instruction == 0 || frame.instruction >= definition->program.size()) return false;
        const auto opcode = definition->program[frame.instruction - 1].opcode;
        SelectionEffect effect;
        Zone zone = Zone::hand; std::uint32_t minimum = 1, maximum = 1;
        bool automatic = false;
        if (opcode == Opcode::exhaust_choice) { effect = SelectionEffect::exhaust; minimum = 0; maximum = source->upgraded ? 5 : 3; }
        else if (opcode == Opcode::upgrade_choice && !source->upgraded) effect = SelectionEffect::upgrade;
        else if (opcode == Opcode::topdeck_choice) { effect = SelectionEffect::topdeck; zone = Zone::discard; automatic = true; }
        else if (opcode == Opcode::exhaust_one || (opcode == Opcode::grit_exhaust && source->upgraded)) effect = SelectionEffect::exhaust;
        else return false;
        maximum = std::min(maximum, static_cast<std::uint32_t>(state.selection_candidates.size()));
        if (p.effect != effect || p.zone != zone || p.minimum != minimum || p.maximum != maximum || p.automatic_at_maximum != automatic) return false;
        std::size_t offered = 0;
        for (auto card : state.zone(zone)) {
            if (effect == SelectionEffect::upgrade && state.cards.get(card) && state.cards.get(card)->upgraded) continue;
            if (offered >= state.selection_candidates.size() || state.selection_candidates[offered++] != card) return false;
        }
        if (offered != state.selection_candidates.size() || offered == 0 || (minimum == 1 && offered <= 1)) return false;
        for (auto card : state.selection_order) if (std::count(state.selection_order.begin(), state.selection_order.end(), card) != 1 || std::find(state.selection_candidates.begin(), state.selection_candidates.end(), card) == state.selection_candidates.end()) return false;
    } else if (state.phase == CombatPhase::discard_choice) {
        if (state.execution.size() != 1 || state.zone(Zone::play).size() != 1 || state.zone(Zone::hand).empty()) return false;
        const auto& frame = state.execution.back(); const auto* card = state.cards.get(frame.source);
        const auto* definition = card ? rules.card(card->definition) : nullptr;
        if (!definition || frame.instruction == 0 || frame.instruction >= definition->program.size() ||
            definition->program[frame.instruction - 1].opcode != Opcode::discard_choice || state.zone(Zone::play)[0] != frame.source) return false;
    } else if (!state.execution.empty() || !state.zone(Zone::play).empty()) return false;
    if (state.phase == CombatPhase::combat_won && (state.player.power_count != 0 || state.enemy.power_count != 0)) return false;
    const auto& counters = state.counters;
    if (counters.finished_plays > counters.started_plays || counters.started_this_turn > counters.started_plays ||
        counters.attacks_this_turn > counters.started_this_turn || counters.zero_energy_attacks_this_turn > counters.attacks_this_turn ||
        counters.finished_ethereal > counters.finished_plays) return false;
    if (!valid_powers(state)) return false;
    for (const auto& modifier : state.cost_modifiers)
        if (!state.cards.get(modifier.card) || static_cast<unsigned>(modifier.kind) > 1 || (static_cast<unsigned>(modifier.expiry) & ~6U) != 0) return false;
    for (const auto& frame : state.execution)
        if (frame.play >= counters.started_plays || !state.cards.get(frame.source) || frame.energy < 0) return false;
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
