#include "gameplay.hpp"
#include <algorithm>
#include <stdexcept>

namespace sts2::engine::gameplay {
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
        const auto remember = listener->remember_choice;
        listener->fired = true;
        auto hand = state.zone(Zone::hand);
        if (kind == ShuffleReactionKind::choose_card) {
            CardHandle receiver;
            if (remember) for (std::size_t i = state.execution.size(); i-- > 0;) {
                const auto& parent = state.execution[i];
                if (std::holds_alternative<std::monostate>(parent.pending)) { receiver = parent.source; break; }
            }
            traversal->remember_to = receiver;
            if (hand.size() > 1) { traversal->waiting = true; begin_selection(state, SelectionEffect::topdeck, Zone::hand, 1, 1, true); }
            else if (!hand.empty()) {
                if (receiver.present()) remember_card_choice(state, receiver, hand.front());
                traversal->remember_to = {};
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
        const auto receiver = choice->remember_to;
        if (!state.cards.get(card)) return fail(state, Restriction::invalid_state);
        if (receiver.present()) remember_card_choice(state, receiver, card);
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
            if (hook->remember_to.present() && !state.cards.get(hook->remember_to)) return false;
            if (hook->begin != captured_end || hook->cursor > hook->count || hook->count > state.captured_shuffle_reactions.size() - captured_end) return false;
            captured_end += hook->count;
            if (hook->waiting && (&frame != &state.execution.back() || state.phase != CombatPhase::card_choice || hook->cursor == 0)) return false;
        } else if (const auto* choice = std::get_if<SelectionContinuation>(&frame.pending)) {
            if (choice->remember_to.present() && !state.cards.get(choice->remember_to)) return false;
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
    if (state.phase == CombatPhase::card_choice) {
        const auto& p = state.selection;
        if (state.execution.empty() || p.continuation_depth != state.execution.size() - 1 || p.id.value == 0 || p.id.value >= state.next_prompt.value ||
            p.minimum > p.maximum || p.maximum == 0 || state.selection_order.size() > p.maximum || p.zone >= Zone::count) return false;
        const auto& frame = state.execution.back();
        SelectionEffect effect;
        Zone zone = Zone::hand; std::uint32_t minimum = 1, maximum = 1;
        bool automatic = false;
        if (const auto* hook = std::get_if<ShuffleContinuation>(&frame.pending)) {
            if (!hook->waiting) return false;
            effect = SelectionEffect::topdeck; automatic = true;
        } else if (std::holds_alternative<std::monostate>(frame.pending)) {
            const auto* source = state.cards.get(frame.source);
            const auto* definition = source ? rules.card(source->definition) : nullptr;
            if (!definition || frame.instruction == 0 || frame.instruction >= definition->program.size()) return false;
            const auto opcode = definition->program[frame.instruction - 1].opcode;
            if (opcode == Opcode::exhaust_choice) { effect = SelectionEffect::exhaust; minimum = 0; maximum = source->upgraded ? 5 : 3; }
            else if (opcode == Opcode::upgrade_choice && !source->upgraded) effect = SelectionEffect::upgrade;
            else if (opcode == Opcode::topdeck_choice) { effect = SelectionEffect::topdeck; zone = Zone::discard; automatic = true; }
            else if (opcode == Opcode::exhaust_one || (opcode == Opcode::grit_exhaust && source->upgraded)) effect = SelectionEffect::exhaust;
            else return false;
        } else return false;
        maximum = std::min(maximum, static_cast<std::uint32_t>(state.selection_candidates.size()));
        if (p.effect != effect || p.zone != zone || p.minimum != minimum || p.maximum != maximum || p.automatic_at_maximum != automatic) return false;
        std::size_t offered = 0;
        for (auto card : state.zone(zone)) {
            if (effect == SelectionEffect::upgrade && state.cards.get(card) && state.cards.get(card)->upgraded) continue;
            if (offered >= state.selection_candidates.size() || state.selection_candidates[offered++] != card) return false;
        }
        if (offered != state.selection_candidates.size() || offered == 0 || (minimum == 1 && offered <= 1)) return false;
        for (auto card : state.selection_order)
            if (std::count(state.selection_order.begin(), state.selection_order.end(), card) != 1 ||
                std::find(state.selection_candidates.begin(), state.selection_candidates.end(), card) == state.selection_candidates.end()) return false;
    } else if (state.phase == CombatPhase::discard_choice) {
        if (state.execution.empty() || state.zone(Zone::hand).empty()) return false;
        const auto& frame = state.execution.back();
        const auto* card = state.cards.get(frame.source); const auto* definition = card ? rules.card(card->definition) : nullptr;
        if (!std::holds_alternative<std::monostate>(frame.pending) || !definition || frame.instruction == 0 || frame.instruction >= definition->program.size() ||
            definition->program[frame.instruction - 1].opcode != Opcode::discard_choice) return false;
    } else if (!state.execution.empty() || !state.zone(Zone::play).empty()) return false;
    return true;
}
}
