#include "interpreter.hpp"
#include "selection.hpp"
#include "../mechanics/cards.hpp"
#include "../mechanics/damage.hpp"
#include "../mechanics/block.hpp"
#include "../mechanics/powers.hpp"
#include "../combat/lifecycle.hpp"
#include "../combat/trace.hpp"
#include "../content/relics.hpp"
#include <algorithm>
#include <limits>

namespace sts2::engine::gameplay {
StepResult execute_instruction(CombatView state, Scratch& scratch, const Ruleset& rules) {
    auto& frame = state.execution.back(); const auto* source = state.cards.get(frame.source);
    if (!source) return fail(state, Restriction::invalid_state);
    const auto* definition = rules.card(source->definition);
    if (!definition || frame.instruction >= definition->program.size()) return fail(state, Restriction::invalid_state);
    auto* target = state.creature(frame.target);
    if (!target) return fail(state, Restriction::invalid_state);
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
        const auto hit = damage(*target, amount, state.player, state.creature_overflow);
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
        (void)apply_power(state, scratch, PowerId::weak, amount, true, false, frame.source, true, false, PowerInstance::Stacking::by_type, {}, frame.target); break;
    case Opcode::vulnerable:
        if (amount > std::numeric_limits<int>::max() - target->power_amount(PowerId::vulnerable, state.creature_overflow)) return fail(state, Restriction::capacity);
        (void)apply_power(state, scratch, PowerId::vulnerable, amount, true, false, frame.source, true, false, PowerInstance::Stacking::by_type, {}, frame.target); break;
    case Opcode::discard_choice:
        if (state.zone(Zone::hand).size() == 1) {
            const auto discarded = state.zone(Zone::hand).front();
            discard_card(state, scratch, discarded);
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
StepResult resume_continuation(CombatView state, Scratch& scratch, const Ruleset& rules) {
    // Cursor updates happen before child work can grow and relocate the stack.
    if (auto* draw_state = std::get_if<DrawContinuation>(&state.execution.back().pending)) {
        if (draw_state->remaining <= 0 || state.zone(Zone::hand).size() >= 10 || state.enemy.hp == 0 || state.player.hp == 0) {
            state.execution.pop_back(); return result(state);
        }
        if (state.zone(Zone::draw).empty()) {
            if (state.zone(Zone::discard).empty()) { state.execution.pop_back(); return result(state); }
            reshuffle(state, rules);
            if (!state.shuffle_order.empty()) {
                auto frame = state.execution.back();
                const auto begin = static_cast<std::uint32_t>(state.captured_shuffle_reactions.size());
                for (auto listener : state.shuffle_order) state.captured_shuffle_reactions.push_back(listener);
                frame.pending = ShuffleContinuation{begin, static_cast<std::uint32_t>(state.shuffle_order.size()), 0};
                state.execution.push_back(frame); return result(state);
            }
        }
        const auto from_hand_draw = draw_state->from_hand_draw;
        --draw_state->remaining;
        draw_one(state, scratch, from_hand_draw);
    } else if (auto* traversal = std::get_if<ShuffleContinuation>(&state.execution.back().pending)) {
        if (traversal->cursor == traversal->count) {
            while (state.captured_shuffle_reactions.size() > traversal->begin) state.captured_shuffle_reactions.pop_back();
            state.execution.pop_back(); return result(state);
        }
        const auto handle = state.captured_shuffle_reactions[traversal->begin + traversal->cursor++];
        auto* listener = state.shuffle_reactions.get(handle);
        if (!listener || listener->fired) return result(state);
        const auto kind = listener->kind;
        listener->fired = true;
        auto hand = state.zone(Zone::hand);
        if (kind == ShuffleReactionKind::choose_card) {
            if (hand.size() > 1) { traversal->waiting = true; begin_selection(state, SelectionEffect::topdeck, Zone::hand, 1, 1, true); }
            else if (!hand.empty()) {
                move_card(state, Zone::hand, Zone::draw, hand.front());
                auto pile = state.zone(Zone::draw); std::rotate(pile.begin(), pile.end() - 1, pile.end());
            }
        } else if (kind == ShuffleReactionKind::nested_draw) {
            if (!hand.empty()) move_card(state, Zone::hand, Zone::discard, hand.front());
            draw(state, scratch, 2, rules, false);
        } else return fail(state, Restriction::invalid_state);
    } else if (auto* choice = std::get_if<SelectionContinuation>(&state.execution.back().pending)) {
        if (choice->cursor == choice->count) {
            while (state.continuation_cards.size() > choice->begin) state.continuation_cards.pop_back();
            state.execution.pop_back(); return result(state);
        }
        const auto card = state.continuation_cards[choice->begin + choice->cursor++];
        const auto effect = choice->effect; const auto zone = choice->zone;
        if (!state.cards.get(card)) return fail(state, Restriction::invalid_state);
        if (effect == SelectionEffect::upgrade) state.cards.get(card)->upgraded = true;
        else if (effect == SelectionEffect::topdeck) {
            move_card(state, zone, Zone::draw, card);
            auto pile = state.zone(Zone::draw); std::rotate(pile.begin(), pile.end() - 1, pile.end());
        } else if (effect == SelectionEffect::exhaust) exhaust(state, scratch, zone, card);
        else return fail(state, Restriction::invalid_state);
    } else if (auto* traversal = std::get_if<ExhaustContinuation>(&state.execution.back().pending)) {
        if (traversal->cursor == traversal->count) { state.execution.pop_back(); return result(state); }
        const auto listener = traversal->candidates[traversal->cursor++];
        react_to_exhaust(listener, state, scratch);
    } else if (std::holds_alternative<RelicDrawCompletion>(state.execution.back().pending)) {
        state.joss_paper_counter %= 5; state.execution.pop_back();
    } else return fail(state, Restriction::invalid_state);
    return result(state);
}

bool valid_continuations(ConstCombatView state, const Ruleset& rules) noexcept {
    if (!state.shuffle_reactions.valid()) return false;
    for (const auto& slot : state.shuffle_reactions.slots())
        if (static_cast<unsigned>(slot.value.kind) > static_cast<unsigned>(ShuffleReactionKind::nested_draw)) return false;
    for (std::size_t i = 0; i < state.shuffle_order.size(); ++i) {
        if (!state.shuffle_reactions.get(state.shuffle_order[i])) return false;
        for (std::size_t j = 0; j < i; ++j) if (state.shuffle_order[i] == state.shuffle_order[j]) return false;
    }
    std::size_t captured_end = 0, cards_end = 0;
    for (const auto& frame : state.execution) {
        const auto* source = state.cards.get(frame.source);
        if (frame.energy < 0 || (frame.source.present() && !source)) return false;
        if (std::holds_alternative<std::monostate>(frame.pending)) {
            const auto* definition = source ? rules.card(source->definition) : nullptr;
            if (!definition || !state.creature(frame.target) || frame.play >= state.counters.started_plays || frame.instruction >= definition->program.size()) return false;
            if (!source->removed && std::find(state.zone(Zone::play).begin(), state.zone(Zone::play).end(), frame.source) == state.zone(Zone::play).end()) return false;
        } else if (const auto* draw_state = std::get_if<DrawContinuation>(&frame.pending)) {
            if (draw_state->remaining < 0) return false;
        } else if (const auto* hook = std::get_if<ShuffleContinuation>(&frame.pending)) {
            if (hook->begin != captured_end || hook->cursor > hook->count || hook->count > state.captured_shuffle_reactions.size() - captured_end) return false;
            captured_end += hook->count;
            if (hook->waiting && (&frame != &state.execution.back() || state.phase != CombatPhase::card_choice || hook->cursor == 0)) return false;
        } else if (const auto* choice = std::get_if<SelectionContinuation>(&frame.pending)) {
            if (choice->begin != cards_end || choice->cursor > choice->count || choice->count > state.continuation_cards.size() - cards_end ||
                choice->effect == SelectionEffect::none || choice->effect > SelectionEffect::topdeck || choice->zone >= Zone::count) return false;
            cards_end += choice->count;
        } else if (const auto* hook = std::get_if<ExhaustContinuation>(&frame.pending)) {
            if (!source || hook->count > hook->candidates.size() || hook->cursor > hook->count) return false;
            for (std::uint8_t i = 0; i < hook->count; ++i) if (static_cast<unsigned>(hook->candidates[i]) > 1) return false;
        } else if (!std::holds_alternative<RelicDrawCompletion>(frame.pending)) return false;
    }
    if (captured_end != state.captured_shuffle_reactions.size() || cards_end != state.continuation_cards.size()) return false;
    for (auto card : state.continuation_cards) if (!state.cards.get(card)) return false;
    for (auto listener : state.captured_shuffle_reactions) if (!listener.present()) return false;
    return valid_selection(state, rules);
}
}
