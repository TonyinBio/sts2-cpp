#include "gameplay.hpp"
#include "sts2/core/sort.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace sts2::engine::gameplay {
void clear_selection(CombatView state) {
    state.selection = {}; state.selection_candidates.clear(); state.selection_order.clear();
}
void begin_selection(CombatView state, SelectionEffect effect, Zone zone, std::uint32_t minimum, std::uint32_t maximum, bool automatic) {
    if (state.next_prompt.value == UINT32_MAX) throw std::length_error("prompt identity exhausted");
    clear_selection(state);
    state.selection = {state.next_prompt, effect, zone, minimum, maximum, automatic};
    ++state.next_prompt.value;
    state.selection.continuation_depth = static_cast<std::uint32_t>(state.execution.size() - 1);
    for (auto card : state.zone(zone))
        if (effect != SelectionEffect::upgrade || !state.cards.get(card)->upgraded) state.selection_candidates.push_back(card);
    state.selection.maximum = std::min(maximum, static_cast<std::uint32_t>(state.selection_candidates.size()));
    state.phase = CombatPhase::card_choice;
}
StepResult execute_instruction(CombatView state, Scratch& scratch, const Ruleset& rules) {
    auto& frame = state.execution.back(); const auto* source = state.cards.get(frame.source);
    if (!source) return fail(state, Restriction::invalid_state);
    const auto* definition = rules.card(source->definition);
    if (!definition || frame.instruction >= definition->program.size()) return fail(state, Restriction::invalid_state);
    const auto instruction = definition->program[frame.instruction++];
    const int amount = instruction.base + (source->upgraded ? instruction.upgrade : 0);
    switch (instruction.opcode) {
    case Opcode::draw: draw(state, scratch, amount, rules, false); break;
    case Opcode::grit_exhaust:
        if (!source->upgraded) {
            const auto& hand = state.zone(Zone::hand);
            if (!hand.empty()) {
                std::int32_t index;
                if (!state.rng.get(RunStream::combat_card_selection).next_int(0, static_cast<std::int32_t>(hand.size()), index)) return fail(state, Restriction::invalid_state);
                exhaust(state, scratch, Zone::hand, hand[index]);
            }
            break;
        }
        [[fallthrough]];
    case Opcode::exhaust_one:
        if (state.zone(Zone::hand).size() == 1) {
            exhaust(state, scratch, Zone::hand, state.zone(Zone::hand).front());
        } else if (!state.zone(Zone::hand).empty()) {
            if (state.next_prompt.value == UINT32_MAX) return fail(state, Restriction::capacity);
            begin_selection(state, SelectionEffect::exhaust, Zone::hand, 1, 1, false); return result(state);
        }
        break;
    case Opcode::topdeck_choice: {
        const auto& candidates = state.zone(Zone::discard);
        if (state.enemy.hp == 0 || candidates.empty()) break;
        if (candidates.size() == 1) {
            const auto card = candidates.front(); move_card(state, Zone::discard, Zone::draw, card);
            auto pile = state.zone(Zone::draw); std::rotate(pile.begin(), pile.end() - 1, pile.end());
        } else {
            if (state.next_prompt.value == UINT32_MAX) return fail(state, Restriction::capacity);
            begin_selection(state, SelectionEffect::topdeck, Zone::discard, 1, 1, true); return result(state);
        }
        break;
    }
    case Opcode::upgrade_choice: {
        std::size_t count = 0;
        for (auto card : state.zone(Zone::hand)) count += !state.cards.get(card)->upgraded;
        if (source->upgraded || count <= 1) {
            for (auto card : state.zone(Zone::hand)) state.cards.get(card)->upgraded = true;
        } else {
            if (state.next_prompt.value == UINT32_MAX) return fail(state, Restriction::capacity);
            begin_selection(state, SelectionEffect::upgrade, Zone::hand, 1, 1, false); return result(state);
        }
        break;
    }
    case Opcode::exhaust_choice:
        if (!state.zone(Zone::hand).empty()) {
            if (state.next_prompt.value == UINT32_MAX) return fail(state, Restriction::capacity);
            begin_selection(state, SelectionEffect::exhaust, Zone::hand, 0, static_cast<std::uint32_t>(amount), false);
            return result(state);
        }
        break;
    case Opcode::damage: {
        const auto hit = damage(state.enemy, amount, state.player);
        emit(state, scratch, CombatEventKind::damaged, false, true, frame.source, 0, 0, hit);
        emit(state, scratch, CombatEventKind::attacked, false, false, frame.source, 0, 0, hit);
        break;
    }
    case Opcode::block: {
        if (!gain_block(state, scratch, amount, frame.source, frame.play)) return fail(state, Restriction::capacity);
        break;
    }
    case Opcode::strength:
        (void)apply_power(state, scratch, PowerId::strength, amount, false, false, frame.source); break;
    case Opcode::weak:
        (void)apply_power(state, scratch, PowerId::weak, amount, true, false, frame.source); break;
    case Opcode::vulnerable:
        if (amount > std::numeric_limits<int>::max() - state.enemy.power_amount(PowerId::vulnerable)) return fail(state, Restriction::capacity);
        (void)apply_power(state, scratch, PowerId::vulnerable, amount, true, false, frame.source); break;
    case Opcode::discard_choice:
        if (state.zone(Zone::hand).size() == 1) {
            const auto discarded = state.zone(Zone::hand).front();
            move_card(state, Zone::hand, Zone::discard, discarded);
            emit(state, scratch, CombatEventKind::discarded, false, false, discarded);
        } else if (!state.zone(Zone::hand).empty()) { state.phase = CombatPhase::discard_choice; return result(state); }
        break;
    case Opcode::finish: {
        const auto card = frame.source; const auto play = frame.play; state.execution.pop_back();
        for (auto it = state.cost_modifiers.begin(); it != state.cost_modifiers.end();) {
            if (it->card == card && (static_cast<unsigned>(it->expiry) & 4U) != 0) it = state.cost_modifiers.erase(it);
            else ++it;
        }
        if (source->removed) {
            // A captured source can outlive its active zone membership.
        } else if (definition->destination == CardDestination::remove) {
            auto pile = state.zone(Zone::play); pile.erase(std::find(pile.begin(), pile.end(), card));
            state.cards.get(card)->removed = true;
        } else if (definition->destination == CardDestination::discard)
            move_card(state, Zone::play, Zone::discard, card);
        if (state.enemy.hp == 0) { end_combat(state, scratch, true); return result(state); }
        ++state.counters.finished_plays;
        state.cards.get(card)->last_finished_player_turn = state.turn;
        emit(state, scratch, CombatEventKind::play_finished, false, false, card, play);
        if (!source->removed && definition->destination == CardDestination::exhaust)
            exhaust(state, scratch, Zone::play, card);
        if (state.enemy.hp == 0) { end_combat(state, scratch, true); return result(state); }
        break;
    }
    }
    return result(state);
}
void react_to_exhaust(ExhaustRelic listener, CombatView state, Scratch& scratch) {
    if (listener == ExhaustRelic::charons_ashes && state.enemy.hp > 0) {
        const auto hit = receive_damage(state.enemy, 3);
        emit(state, scratch, CombatEventKind::damaged, false, true, {}, 4, 0, hit);
    } else if (listener == ExhaustRelic::joss_paper) {
        ++state.joss_paper_counter;
        if (state.joss_paper_counter >= 5) {
            if (!state.shuffle_order.empty()) {
                EffectFrame completion;
                if (!state.execution.empty()) completion = state.execution.back();
                completion.pending = RelicDrawCompletion{};
                state.execution.push_back(completion);
                draw(state, scratch, static_cast<int>(state.joss_paper_counter / 5), Ruleset::pinned(), false);
                return;
            }
            draw(state, scratch, static_cast<int>(state.joss_paper_counter / 5), Ruleset::pinned(), false);
            state.joss_paper_counter %= 5;
        }
    }
}
}
