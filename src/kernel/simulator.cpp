#include "sts2/kernel/simulator.hpp"
#include "sts2/core/sort.hpp"
#include <algorithm>
#include <limits>
#include <new>
#include <stdexcept>

namespace sts2::engine {
namespace {
constexpr std::array starting_deck = {
    CardSpec{CardId::strike}, CardSpec{CardId::strike}, CardSpec{CardId::strike},
    CardSpec{CardId::strike}, CardSpec{CardId::strike}, CardSpec{CardId::defend},
    CardSpec{CardId::defend}, CardSpec{CardId::defend}, CardSpec{CardId::defend}, CardSpec{CardId::bash}};
StepResult result(const State& state, Restriction reason = Restriction::none) noexcept {
    if (reason != Restriction::none) return {StepStatus::invalid_action, state.boundary, reason};
    switch (state.phase) {
    case Phase::combat_won: return {StepStatus::combat_won, state.boundary};
    case Phase::player_died: return {StepStatus::player_died, state.boundary};
    case Phase::uninitialized: case Phase::invalid: return {StepStatus::engine_error, state.boundary, Restriction::invalid_state};
    default: return {StepStatus::awaiting_input, state.boundary};
    }
}
StepResult fail(State& state, Restriction reason) noexcept {
    state.phase = Phase::invalid;
    return {StepStatus::engine_error, state.boundary, reason};
}
bool consume(Scratch& scratch) noexcept { return ++scratch.steps <= scratch.step_limit; }
void history(State& s, HistoryKind kind, bool enemy_side = false, bool actor_enemy = false,
             CardHandle card = {}, std::uint32_t object = 0, int amount = 0, HitResult hit = {}) {
    s.history.push_back({kind, s.turn, enemy_side, actor_enemy, card, object, amount, hit});
}
std::uint32_t apply_power(State& s, PowerId id, int amount, bool enemy, bool enemy_side) {
    auto found = std::find_if(s.powers.begin(), s.powers.end(), [&](const auto& p) { return p.active && p.definition == id && p.enemy_owner == enemy; });
    std::uint32_t index;
    if (found == s.powers.end()) {
        index = static_cast<std::uint32_t>(s.powers.size()); s.powers.push_back({id, amount, 0, enemy, true, !enemy});
    } else { index = static_cast<std::uint32_t>(found - s.powers.begin()); found->amount += amount; }
    history(s, HistoryKind::power_received, enemy_side, enemy, {}, index, amount);
    if (enemy) ++s.debuffs_applied;
    return index;
}
void move_card(State& state, Zone from, Zone to, CardHandle card) {
    auto& source = state.zone(from);
    source.erase(std::find(source.begin(), source.end(), card));
    state.zone(to).push_back(card);
}
void exhaust(State& state, Zone from, CardHandle card) {
    move_card(state, from, Zone::exhaust, card);
    history(state, HistoryKind::exhausted, false, false, card);
}
void draw(State& state, int count, const Ruleset& rules) {
    auto& hand = state.zone(Zone::hand); auto& pile = state.zone(Zone::draw);
    auto& discard = state.zone(Zone::discard);
    while (count-- > 0 && hand.size() < 10) {
        if (pile.empty() && !discard.empty()) {
            pile.insert(pile.end(), discard.begin(), discard.end()); discard.clear();
            game_sort<CardHandle>(pile, [&](CardHandle a, CardHandle b) {
                const auto& ca = *state.cards.get(a); const auto& cb = *state.cards.get(b);
                const auto ida = rules.card(ca.definition)->model_id, idb = rules.card(cb.definition)->model_id;
                return ida == idb ? ca.upgraded < cb.upgraded : ida < idb;
            });
            state.rng.get(RunStream::shuffle).shuffle<CardHandle>(pile); ++state.shuffles;
        }
        if (pile.empty()) break;
        auto card = pile.front(); pile.erase(pile.begin()); hand.push_back(card); ++state.cards_drawn; history(state, HistoryKind::drawn, false, false, card);
    }
}
HitResult damage(CreatureState& target, int base, const CreatureState& source) noexcept {
    // The native decimal modifiers combine before a single floor operation.
    std::int64_t numerator = std::int64_t(base) * (source.shrink ? 70 : 100) * (target.vulnerable > 0 ? 150 : 100);
    const auto amount = static_cast<int>(numerator / 10000);
    const auto blocked = std::min(target.block, amount);
    const auto unblocked = std::min(target.hp, amount - blocked);
    const HitResult hit{blocked, unblocked, amount - blocked - unblocked,
        target.block > 0 && blocked == target.block, blocked >= amount, unblocked == target.hp};
    target.block -= blocked; target.hp -= unblocked;
    return hit;
}
void end_combat(State& state, bool won) noexcept {
    state.phase = won ? Phase::combat_won : Phase::player_died;
    state.execution.clear();
    if (won) {
        state.history.clear(); state.plays.clear(); state.powers.clear();
        for (auto& zone : state.zones) zone.clear();
    } else {
        for (auto& power : state.powers) if (!power.enemy_owner) power.active = false;
    }
    state.player.block = 0; state.player.shrink = false; state.player.vulnerable = 0;
    if (won && state.burning_blood) {
        const auto healed = std::min(6, state.player.max_hp - state.player.hp);
        state.player.hp += healed; state.healing += healed;
    }
}
}
void State::reserve(std::size_t card_count) {
    cards.reserve(card_count);
    for (auto& zone : zones) zone.reserve(card_count);
    execution.reserve(8); history.reserve(128); plays.reserve(32); powers.reserve(8);
    selection.candidates.reserve(card_count); selection.selected.reserve(card_count);
}
std::size_t State::retained_bytes() const noexcept {
    const auto text_address = reinterpret_cast<std::uintptr_t>(seed.data());
    const auto root_address = reinterpret_cast<std::uintptr_t>(this);
    const auto seed_bytes = text_address >= root_address && text_address < root_address + sizeof(State) ? 0 : seed.capacity() + 1;
    auto bytes = sizeof(State) + seed_bytes + cards.retained_bytes() + execution.capacity() * sizeof(EffectFrame);
    bytes += (selection.candidates.capacity() + selection.selected.capacity()) * sizeof(CardHandle);
    bytes += history.capacity() * sizeof(CombatHistoryEntry) + plays.capacity() * sizeof(CardPlayRecord) + powers.capacity() * sizeof(PowerRecord);
    for (const auto& zone : zones) bytes += zone.capacity() * sizeof(CardHandle);
    return bytes;
}
void clone_into(State& destination, const State& source) {
    if (&destination == &source) return;
    try {
        destination = source;
        destination.reserve(source.cards.slots().size());
    } catch (...) { destination.phase = Phase::invalid; throw; }
}
StepResult Simulator::initialize(State& state, const CombatConfig& config, Scratch& scratch) const {
    if (config.build != game_build || config.ascension != 0 || config.encounter != "ENCOUNTER.SHRINKER_BEETLE_WEAK" ||
        config.seed.empty() || config.seed.size() > 4096 || config.player_max_hp < 1 || config.player_hp < 1 || config.player_hp > config.player_max_hp ||
        config.card_generation == 0 || config.enemy_hp < 0 || config.enemy_hp > 40 ||
        (config.seed.starts_with("old") && std::any_of(config.seed.begin(), config.seed.end(), [](unsigned char c) { return c >= 128; })))
        return {StepStatus::unsupported_content, state.boundary, Restriction::unsupported_setup};
    const auto deck = config.deck.empty() ? std::span<const CardSpec>(starting_deck) : config.deck;
    if (deck.size() > limits_.cards) return {StepStatus::resource_limit, state.boundary, Restriction::capacity};
    for (const auto& card : deck) if (!rules_.card(card.definition))
        return {StepStatus::unsupported_content, state.boundary, Restriction::unsupported_setup};
    State next;
    if (!next.clock.initialize(config.clock)) return {StepStatus::unsupported_content, state.boundary, Restriction::unsupported_setup};
    try {
        next.reserve(deck.size()); next.seed = config.seed; next.numeric_seed = game_seed(config.seed); next.rng = config.initial_rng ? *config.initial_rng : RngBank(config.seed);
        next.player = {config.player_hp, config.player_max_hp};
        int enemy_hp = config.enemy_hp;
        if (enemy_hp == 0) (void)next.rng.get(RunStream::niche).next_int(38, 41, enemy_hp);
        next.enemy = {enemy_hp, enemy_hp}; next.energy = 3; next.turn = 1; next.burning_blood = config.burning_blood;
        for (const auto& card : deck) {
            auto handle = next.cards.create({card.definition, card.upgraded, next.next_creation_order++, card.deck_instance}, limits_.cards, config.card_generation);
            if (!handle.present()) return {StepStatus::resource_limit, state.boundary, Restriction::capacity};
            next.zone(Zone::draw).push_back(handle);
        }
        next.rng.get(RunStream::shuffle).shuffle<CardHandle>(next.zone(Zone::draw)); ++next.shuffles;
        draw(next, 5, rules_); next.phase = Phase::play; next.boundary.value = 1;
    } catch (const std::bad_alloc&) { return {StepStatus::resource_limit, state.boundary, Restriction::capacity}; }
    state = std::move(next); scratch.steps = 0;
    return result(state);
}
Restriction Simulator::validate(const State& state, const Input& input) const noexcept {
    if (state.phase == Phase::uninitialized || state.phase == Phase::invalid || state.rules != rules_revision)
        return Restriction::invalid_state;
    if (input.expected_boundary != state.boundary) return Restriction::stale_boundary;
    if (state.phase == Phase::combat_won || state.phase == Phase::player_died) return Restriction::wrong_phase;
    if (state.phase == Phase::card_choice) {
        if (input.prompt != state.selection.id) return Restriction::invalid_selection;
        if (input.target_enemy) return Restriction::malformed_action;
        const auto& selection = state.selection;
        if (input.kind == InputKind::confirm_selection) {
            if (input.card != CardHandle{}) return Restriction::malformed_action;
            return selection.selected.size() >= selection.minimum && selection.selected.size() <= selection.maximum ? Restriction::none : Restriction::invalid_selection;
        }
        if (input.kind != InputKind::select_card && input.kind != InputKind::deselect_card) return Restriction::wrong_phase;
        const auto selected = std::find(selection.selected.begin(), selection.selected.end(), input.card) != selection.selected.end();
        if (input.kind == InputKind::deselect_card) return selected ? Restriction::none : Restriction::invalid_selection;
        return !selected && std::find(selection.candidates.begin(), selection.candidates.end(), input.card) != selection.candidates.end() ? Restriction::none : Restriction::invalid_selection;
    }
    if (input.prompt != PromptId{}) return Restriction::invalid_selection;
    if (input.kind == InputKind::end_turn) {
        if (input.card != CardHandle{} || input.target_enemy) return Restriction::malformed_action;
        return state.phase == Phase::play ? Restriction::none : Restriction::wrong_phase;
    }
    if (input.kind != InputKind::play_card && input.kind != InputKind::select_card) return Restriction::malformed_action;
    if (input.kind == InputKind::select_card) {
        if (state.phase != Phase::discard_choice) return Restriction::wrong_phase;
        if (input.target_enemy) return Restriction::malformed_action;
    } else if (state.phase != Phase::play) return Restriction::wrong_phase;
    const auto* card = state.cards.get(input.card);
    const auto& hand = state.zone(Zone::hand);
    if (!card || std::find(hand.begin(), hand.end(), input.card) == hand.end()) return Restriction::stale_card;
    if (input.kind == InputKind::select_card) return Restriction::none;
    const auto* definition = rules_.card(card->definition);
    if (!definition) return Restriction::invalid_state;
    if (definition->enemy_target != input.target_enemy || (input.target_enemy && state.enemy.hp <= 0)) return Restriction::invalid_target;
    if (state.energy < definition->cost) return Restriction::insufficient_energy;
    return Restriction::none;
}
StepResult Simulator::advance(State& state, Scratch& scratch) const noexcept try {
    if (state.phase != Phase::play || state.execution.empty()) return result(state);
    while (!state.execution.empty()) {
        if (!consume(scratch)) return fail(state, Restriction::execution_budget);
        auto& frame = state.execution.back(); const auto* source = state.cards.get(frame.source);
        if (!source) return fail(state, Restriction::invalid_state);
        const auto* definition = rules_.card(source->definition);
        if (!definition || frame.instruction >= definition->program.size()) return fail(state, Restriction::invalid_state);
        const auto instruction = definition->program[frame.instruction++];
        const int amount = instruction.base + (source->upgraded ? instruction.upgrade : 0);
        switch (instruction.opcode) {
        case Opcode::draw: draw(state, amount, rules_); break;
        case Opcode::grit_exhaust:
            if (!source->upgraded) {
                const auto& hand = state.zone(Zone::hand);
                if (!hand.empty()) {
                    std::int32_t index;
                    if (!state.rng.get(RunStream::combat_card_selection).next_int(0, static_cast<std::int32_t>(hand.size()), index)) return fail(state, Restriction::invalid_state);
                    exhaust(state, Zone::hand, hand[index]);
                }
                break;
            }
            [[fallthrough]];
        case Opcode::exhaust_one:
            if (state.zone(Zone::hand).size() == 1) {
                exhaust(state, Zone::hand, state.zone(Zone::hand).front());
            } else if (!state.zone(Zone::hand).empty()) {
                if (state.next_prompt.value == UINT32_MAX) return fail(state, Restriction::capacity);
                state.selection = {state.next_prompt, SelectionEffect::exhaust, Zone::hand, 1, 1, false, state.zone(Zone::hand), {}};
                ++state.next_prompt.value; state.phase = Phase::card_choice; return result(state);
            }
            break;
        case Opcode::topdeck_choice: {
            const auto& candidates = state.zone(Zone::discard);
            if (state.enemy.hp == 0 || candidates.empty()) break;
            if (candidates.size() == 1) {
                const auto card = candidates.front(); move_card(state, Zone::discard, Zone::draw, card);
                auto& pile = state.zone(Zone::draw); std::rotate(pile.begin(), pile.end() - 1, pile.end());
            } else {
                if (state.next_prompt.value == UINT32_MAX) return fail(state, Restriction::capacity);
                state.selection = {state.next_prompt, SelectionEffect::topdeck, Zone::discard, 1, 1, true, candidates, {}};
                ++state.next_prompt.value; state.phase = Phase::card_choice; return result(state);
            }
            break;
        }
        case Opcode::upgrade_choice: {
            auto candidates = state.zone(Zone::hand);
            std::erase_if(candidates, [&](auto card) { return state.cards.get(card)->upgraded; });
            if (source->upgraded || candidates.size() <= 1) {
                for (auto card : candidates) state.cards.get(card)->upgraded = true;
            } else {
                if (state.next_prompt.value == UINT32_MAX) return fail(state, Restriction::capacity);
                state.selection = {state.next_prompt, SelectionEffect::upgrade, Zone::hand, 1, 1, false, std::move(candidates), {}};
                ++state.next_prompt.value; state.phase = Phase::card_choice; return result(state);
            }
            break;
        }
        case Opcode::exhaust_choice:
            if (!state.zone(Zone::hand).empty()) {
                if (state.next_prompt.value == UINT32_MAX) return fail(state, Restriction::capacity);
                state.selection = {state.next_prompt, SelectionEffect::exhaust, Zone::hand, 0, static_cast<std::uint32_t>(amount), false, state.zone(Zone::hand), {}};
                ++state.next_prompt.value;
                state.phase = Phase::card_choice;
                return result(state);
            }
            break;
        case Opcode::damage: {
            const auto hit = damage(state.enemy, amount, state.player);
            state.damage_dealt += hit.unblocked;
            history(state, HistoryKind::damaged, false, true, frame.source, 0, 0, hit);
            history(state, HistoryKind::attacked, false, false, frame.source, 0, 0, hit);
            break;
        }
        case Opcode::block:
            if (amount > std::numeric_limits<int>::max() - state.player.block) return fail(state, Restriction::capacity);
            state.player.block += amount; history(state, HistoryKind::block_gained, false, false, frame.source, frame.play, amount); break;
        case Opcode::vulnerable:
            if (amount > std::numeric_limits<int>::max() - state.enemy.vulnerable) return fail(state, Restriction::capacity);
            state.enemy.vulnerable += amount; (void)apply_power(state, PowerId::vulnerable, amount, true, false); break;
        case Opcode::discard_choice:
            if (state.zone(Zone::hand).size() == 1) {
                const auto discarded = state.zone(Zone::hand).front();
                move_card(state, Zone::hand, Zone::discard, discarded);
                history(state, HistoryKind::discarded, false, false, discarded);
            } else if (!state.zone(Zone::hand).empty()) { state.phase = Phase::discard_choice; return result(state); }
            break;
        case Opcode::finish: {
            const auto card = frame.source; const auto play = frame.play; state.execution.pop_back();
            const bool exhaust_source = source->definition == CardId::purity;
            if (!exhaust_source) move_card(state, Zone::play, Zone::discard, card);
            if (state.enemy.hp == 0) { end_combat(state, true); return result(state); }
            history(state, HistoryKind::play_finished, false, false, card, play);
            if (exhaust_source) exhaust(state, Zone::play, card);
            break;
        }
        }
    }
    return result(state);
} catch (const std::bad_alloc&) { state.phase = Phase::invalid; return {StepStatus::resource_limit, state.boundary, Restriction::capacity}; }
catch (...) { return fail(state, Restriction::invalid_state); }
StepResult Simulator::apply(State& state, const Input& input, Scratch& scratch) const noexcept try {
    const auto restriction = validate(state, input);
    if (restriction != Restriction::none) return result(state, restriction);
    if (state.boundary.value == std::numeric_limits<std::uint32_t>::max()) return fail(state, Restriction::capacity);
    if (!state.clock.advance_action()) return fail(state, Restriction::clock_exhausted);
    scratch.steps = 0;
    if (state.phase == Phase::card_choice) {
        auto& selected = state.selection.selected;
        if (input.kind == InputKind::select_card) {
            if (selected.size() == state.selection.maximum) selected.pop_back();
            selected.push_back(input.card);
        } else if (input.kind == InputKind::deselect_card) {
            selected.erase(std::find(selected.begin(), selected.end(), input.card));
        }
        if (input.kind == InputKind::confirm_selection || (state.selection.automatic_at_maximum && selected.size() == state.selection.maximum)) {
            for (auto card : selected) {
                if (state.selection.effect == SelectionEffect::upgrade) state.cards.get(card)->upgraded = true;
                else if (state.selection.effect == SelectionEffect::topdeck) {
                    move_card(state, state.selection.zone, Zone::draw, card);
                    auto& pile = state.zone(Zone::draw); std::rotate(pile.begin(), pile.end() - 1, pile.end());
                } else exhaust(state, state.selection.zone, card);
            }
            state.selection = {};
            state.phase = Phase::play;
            const auto advanced = advance(state, scratch);
            if (advanced.status == StepStatus::engine_error || advanced.status == StepStatus::resource_limit) return advanced;
        }
    } else if (input.kind == InputKind::end_turn) {
        if (!consume(scratch)) return fail(state, Restriction::execution_budget);
        auto& hand = state.zone(Zone::hand); auto& discard = state.zone(Zone::discard);
        for (auto card : hand) if (state.cards.get(card)->definition != CardId::purity) discard.push_back(card);
        std::erase_if(hand, [&](auto card) { return state.cards.get(card)->definition != CardId::purity; });
        state.enemy.block = 0;
        for (auto& p : state.powers) if (p.active && p.enemy_owner) p.amount_on_turn_start = p.amount;
        switch (state.move) {
        case BeetleMove::shrink: state.player.shrink = true; (void)apply_power(state, PowerId::shrink, -1, false, true); state.move = BeetleMove::chomp; break;
        case BeetleMove::chomp: case BeetleMove::stomp: {
            const auto hit = damage(state.player, state.move == BeetleMove::chomp ? 7 : 13, state.enemy);
            state.damage_taken += hit.unblocked;
            history(state, HistoryKind::damaged, true, false, {}, 0, 0, hit);
            history(state, HistoryKind::attacked, true, true, {}, 0, 0, hit);
            if (state.player.hp > 0) state.move = state.move == BeetleMove::chomp ? BeetleMove::stomp : BeetleMove::chomp;
            break;
        }
        }
        if (state.player.hp == 0) end_combat(state, false);
        else {
            if (state.enemy.vulnerable > 0) {
                --state.enemy.vulnerable;
                for (std::size_t i = 0; i < state.powers.size(); ++i) {
                    auto& p = state.powers[i];
                    if (p.active && p.definition == PowerId::vulnerable && p.enemy_owner) {
                        p.amount = state.enemy.vulnerable; if (!p.amount) p.active = false;
                        history(state, HistoryKind::power_received, true, true, {}, static_cast<std::uint32_t>(i), -1);
                    }
                }
            }
            for (auto& p : state.powers) if (p.active && !p.enemy_owner) p.amount_on_turn_start = p.amount;
            state.player.block = 0;
            if (state.turn == std::numeric_limits<int>::max()) return fail(state, Restriction::capacity);
            ++state.turn; state.energy = 3; draw(state, 5, rules_);
        }
    } else if (input.kind == InputKind::play_card) {
        const auto* card = state.cards.get(input.card);
        state.energy -= rules_.card(card->definition)->cost;
        move_card(state, Zone::hand, Zone::play, input.card);
        const auto play_id = static_cast<std::uint32_t>(state.plays.size());
        const auto cost = rules_.card(card->definition)->cost;
        state.plays.push_back({input.card, cost, input.target_enemy});
        history(state, HistoryKind::energy_spent, false, false, {}, 0, cost);
        history(state, HistoryKind::play_started, false, false, input.card, play_id);
        state.execution.push_back({input.card, 0, play_id}); ++state.cards_played;
        const auto advanced = advance(state, scratch);
        if (advanced.status == StepStatus::engine_error || advanced.status == StepStatus::resource_limit) return advanced;
    } else {
        move_card(state, Zone::hand, Zone::discard, input.card);
        history(state, HistoryKind::discarded, false, false, input.card);
        state.phase = Phase::play;
        const auto advanced = advance(state, scratch);
        if (advanced.status == StepStatus::engine_error || advanced.status == StepStatus::resource_limit) return advanced;
    }
    if (state.phase == Phase::invalid) return {StepStatus::engine_error, state.boundary, Restriction::execution_budget};
    ++state.boundary.value;
    return result(state);
} catch (const std::bad_alloc&) {
    state.phase = Phase::invalid;
    return {StepStatus::resource_limit, state.boundary, Restriction::capacity};
} catch (...) { return fail(state, Restriction::invalid_state); }
bool valid_state(const State& state, const Ruleset& rules) noexcept {
    if (static_cast<unsigned>(state.phase) > static_cast<unsigned>(Phase::invalid) ||
        static_cast<unsigned>(state.move) > static_cast<unsigned>(BeetleMove::stomp) ||
        state.seed.empty() || state.seed.size() > 4096 || state.numeric_seed != game_seed(state.seed) ||
        state.rules != rules_revision || state.phase == Phase::uninitialized || state.phase == Phase::invalid ||
        state.boundary.value == 0 || state.player.max_hp < 1 || state.player.hp < 0 || state.player.hp > state.player.max_hp ||
        state.enemy.max_hp < 1 || state.enemy.hp < 0 || state.enemy.hp > state.enemy.max_hp ||
        state.player.block < 0 || state.enemy.block < 0 || state.enemy.vulnerable < 0 || state.player.vulnerable < 0 ||
        state.energy < 0 || state.turn < 1 || state.zone(Zone::hand).size() > 10) return false;
    if (state.clock.elapsed_seconds() != std::int64_t(state.boundary.value - 1) * state.clock.config().seconds_per_action) return false;
    for (const auto& stream : state.rng.streams) {
        const auto& s = stream.state().words;
        if ((s[0] | s[1] | s[2] | s[3]) == 0) return false;
    }
    const bool terminal = state.phase == Phase::combat_won || state.phase == Phase::player_died;
    if ((state.phase == Phase::combat_won && (state.enemy.hp != 0 || state.player.hp == 0)) ||
        (state.phase == Phase::player_died && state.player.hp != 0) ||
        (!terminal && (state.enemy.hp == 0 || state.player.hp == 0))) return false;
    if (state.next_prompt.value == 0 || (state.phase != Phase::card_choice && state.selection != CardSelection{})) return false;
    if (state.phase == Phase::card_choice) {
        const auto& p = state.selection;
        if (state.execution.size() != 1 || state.zone(Zone::play).size() != 1 ||
            p.id.value == 0 || p.id.value >= state.next_prompt.value || p.minimum > p.maximum ||
            p.maximum == 0 || p.selected.size() > p.maximum || static_cast<unsigned>(p.zone) >= static_cast<unsigned>(Zone::count)) return false;
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
        if (p.effect != effect || p.zone != zone || p.minimum != minimum || p.maximum != maximum || p.automatic_at_maximum != automatic) return false;
        std::size_t offered = 0;
        for (auto card : state.zone(zone)) {
            if (effect == SelectionEffect::upgrade && state.cards.get(card) && state.cards.get(card)->upgraded) continue;
            if (offered >= p.candidates.size() || p.candidates[offered++] != card) return false;
        }
        if (offered != p.candidates.size() || offered == 0 || (minimum == 1 && offered <= 1)) return false;
        for (auto card : p.selected) if (std::count(p.selected.begin(), p.selected.end(), card) != 1 || std::find(p.candidates.begin(), p.candidates.end(), card) == p.candidates.end()) return false;
    } else if (state.phase == Phase::discard_choice) {
        if (state.execution.size() != 1 || state.zone(Zone::play).size() != 1 || state.zone(Zone::hand).empty()) return false;
        const auto& frame = state.execution.back(); const auto* card = state.cards.get(frame.source);
        if (!card || card->definition != CardId::survivor || frame.instruction != 2 || state.zone(Zone::play)[0] != frame.source) return false;
    } else if (!state.execution.empty() || !state.zone(Zone::play).empty()) return false;
    if (state.phase == Phase::combat_won && (!state.history.empty() || !state.plays.empty() || !state.powers.empty())) return false;
    if (state.damage_dealt < 0 || state.damage_taken < 0 || state.debuffs_applied < 0 || state.healing < 0) return false;
    for (const auto& play : state.plays) if (!state.cards.get(play.card) || play.energy < 0) return false;
    for (const auto& p : state.powers) if (static_cast<unsigned>(p.definition) > 1) return false;
    for (const auto& entry : state.history) {
        if (static_cast<unsigned>(entry.kind) > static_cast<unsigned>(HistoryKind::exhausted) || entry.turn < 1 || entry.turn > state.turn) return false;
        if (entry.card.present() && !state.cards.get(entry.card)) return false;
        if ((entry.kind == HistoryKind::play_started || entry.kind == HistoryKind::play_finished || entry.kind == HistoryKind::block_gained) && entry.object >= state.plays.size()) return false;
        if (entry.kind == HistoryKind::power_received && entry.object >= state.powers.size()) return false;
        if (entry.hit.blocked < 0 || entry.hit.unblocked < 0 || entry.hit.overkill < 0) return false;
    }
    for (const auto& frame : state.execution) if (frame.play >= state.plays.size() || state.plays[frame.play].card != frame.source) return false;
    std::size_t live = 0, membership = 0;
    for (const auto& slot : state.cards.slots()) {
        if (slot.generation == 0 || !rules.card(slot.value.definition) || slot.value.creation_order >= state.next_creation_order) return false;
        if (!slot.live) continue;
        ++live;
        if (slot.generation == 0 || !rules.card(slot.value.definition) || slot.value.creation_order >= state.next_creation_order) return false;
    }
    for (std::size_t a = 0; a < state.zones.size(); ++a) {
        const auto& zone = state.zones[a]; membership += zone.size();
        if (state.phase == Phase::combat_won && !zone.empty()) return false;
        for (std::size_t i = 0; i < zone.size(); ++i) {
            if (!state.cards.get(zone[i])) return false;
            for (std::size_t b = 0; b <= a; ++b) {
                const auto end = b == a ? i : state.zones[b].size();
                for (std::size_t j = 0; j < end; ++j) if (zone[i] == state.zones[b][j]) return false;
            }
        }
    }
    return state.phase == Phase::combat_won ? membership == 0 : membership == live;
}
} // namespace sts2::engine
