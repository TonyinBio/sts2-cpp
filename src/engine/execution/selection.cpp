#include "selection.hpp"
#include "../combat/lifecycle.hpp"
#include <algorithm>
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
StepResult apply_card_selection(CombatView state, const CombatInput& input, Scratch& scratch, const Ruleset& rules) {
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
        resolution.pending = SelectionContinuation{state.selection.effect, state.selection.zone, begin, static_cast<std::uint32_t>(selected.size()), 0};
        if (auto* hook = std::get_if<ShuffleContinuation>(&state.execution.back().pending)) hook->waiting = false;
        clear_selection(state); state.phase = CombatPhase::play;
        state.execution.push_back(resolution);
        const auto advanced = advance_combat(state, scratch, rules);
        if (advanced.status == StepStatus::engine_error || advanced.status == StepStatus::resource_limit) return advanced;
    }
    return result(state);
}

Restriction validate_selection_input(ConstCombatView state, const CombatInput& input) noexcept {
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

bool valid_selection(ConstCombatView state, const Ruleset& rules) noexcept {
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
