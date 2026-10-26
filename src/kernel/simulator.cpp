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
StepResult result(ConstStateView state, Restriction reason = Restriction::none) noexcept {
    if (reason != Restriction::none) return {StepStatus::invalid_action, state.boundary, reason};
    switch (state.phase) {
    case Phase::combat_won: return {StepStatus::combat_won, state.boundary};
    case Phase::player_died: return {StepStatus::player_died, state.boundary};
    case Phase::uninitialized: case Phase::invalid: return {StepStatus::engine_error, state.boundary, Restriction::invalid_state};
    default: return {StepStatus::awaiting_input, state.boundary};
    }
}
StepResult fail(StateView state, Restriction reason) noexcept {
    state.phase = Phase::invalid;
    return {StepStatus::engine_error, state.boundary, reason};
}
bool consume(Scratch& scratch) noexcept { return ++scratch.steps <= scratch.step_limit; }
void history(StateView s, HistoryKind kind, bool enemy_side = false, bool actor_enemy = false,
             CardHandle card = {}, std::uint32_t object = 0, int amount = 0, HitResult hit = {}) {
    s.history.push_back({kind, s.turn, enemy_side, actor_enemy, card, object, amount, hit});
}
std::uint32_t apply_power(StateView s, PowerId id, int amount, bool enemy, bool enemy_side) {
    auto found = std::find_if(s.powers.begin(), s.powers.end(), [&](const auto& p) { return p.active && p.definition == id && p.enemy_owner == enemy; });
    std::uint32_t index;
    if (found == s.powers.end()) {
        index = static_cast<std::uint32_t>(s.powers.size()); s.powers.push_back({id, amount, 0, enemy, true, !enemy});
    } else { index = static_cast<std::uint32_t>(found - s.powers.begin()); found->amount += amount; }
    history(s, HistoryKind::power_received, enemy_side, enemy, {}, index, amount);
    return index;
}
void move_card(StateView state, Zone from, Zone to, CardHandle card) {
    auto source = state.zone(from);
    source.erase(std::find(source.begin(), source.end(), card));
    state.zone(to).push_back(card);
}
void draw(StateView state, int count, const Ruleset& rules) {
    auto hand = state.zone(Zone::hand); auto pile = state.zone(Zone::draw);
    auto discard = state.zone(Zone::discard);
    while (count-- > 0 && hand.size() < 10) {
        if (pile.empty() && !discard.empty()) {
            pile.insert(pile.end(), discard.begin(), discard.end()); discard.clear();
            game_sort<CardHandle>(pile, [&](CardHandle a, CardHandle b) {
                const auto& ca = *state.cards.get(a); const auto& cb = *state.cards.get(b);
                const auto ida = rules.card(ca.definition)->model_id, idb = rules.card(cb.definition)->model_id;
                return ida == idb ? ca.upgraded < cb.upgraded : ida < idb;
            });
            state.rng.get(RunStream::shuffle).shuffle<CardHandle>(pile);
        }
        if (pile.empty()) break;
        auto card = pile.front(); pile.erase(pile.begin()); hand.push_back(card); history(state, HistoryKind::drawn, false, false, card);
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
void end_combat(StateView state, bool won) {
    state.phase = won ? Phase::combat_won : Phase::player_died;
    state.execution.clear();
    if (won) {
        state.history.clear(); state.plays.clear(); state.powers.clear();
        for (auto& zone : state.zone_views()) zone.clear();
    } else {
        for (auto& power : state.powers) if (!power.enemy_owner) power.active = false;
    }
    state.player.block = 0; state.player.shrink = false; state.player.vulnerable = 0;
    if (won && state.burning_blood) {
        const auto healed = std::min(6, state.player.max_hp - state.player.hp);
        state.player.hp += healed;
    }
    if (won) {
        // Terminal combat has no suspended work or visible piles. Preserve all
        // scalars/identity progress, but release its completed collection graph.
        State terminal(state.overflow.allocator());
        std::memcpy(&terminal.record, &state.record, sizeof(StateRecord));
        terminal.record.for_each_sequence([](auto& sequence) { sequence = std::remove_reference_t<decltype(sequence)>{}; });
        terminal.view().seed = std::string_view(state.seed);
        state.replace(std::move(terminal));
    }
}
}
StepResult Simulator::initialize(StateView state, const CombatConfig& config, Scratch& scratch) const {
    if (config.build != game_build || config.ascension != 0 || config.encounter != "ENCOUNTER.SHRINKER_BEETLE_WEAK" ||
        config.seed.empty() || config.seed.size() > 4096 || config.player_max_hp < 1 || config.player_hp < 1 || config.player_hp > config.player_max_hp ||
        config.card_generation == 0 || config.enemy_hp < 0 || config.enemy_hp > 40 ||
        (config.seed.starts_with("old") && std::any_of(config.seed.begin(), config.seed.end(), [](unsigned char c) { return c >= 128; })))
        return {StepStatus::unsupported_content, state.boundary, Restriction::unsupported_setup};
    const auto deck = config.deck.empty() ? std::span<const CardSpec>(starting_deck) : config.deck;
    if (deck.size() > limits_.cards) return {StepStatus::resource_limit, state.boundary, Restriction::capacity};
    for (const auto& card : deck) if (!rules_.card(card.definition))
        return {StepStatus::unsupported_content, state.boundary, Restriction::unsupported_setup};
    State next(state.overflow.allocator()); auto next_view = next.view();
    if (!next_view.clock.initialize(config.clock)) return {StepStatus::unsupported_content, state.boundary, Restriction::unsupported_setup};
    try {
        next_view.reserve(deck.size()); next_view.seed = config.seed; next_view.numeric_seed = game_seed(config.seed); next_view.rng = config.initial_rng ? *config.initial_rng : RngBank(config.seed);
        next_view.player = {config.player_hp, config.player_max_hp};
        int enemy_hp = config.enemy_hp;
        if (enemy_hp == 0) (void)next_view.rng.get(RunStream::niche).next_int(38, 41, enemy_hp);
        next_view.enemy = {enemy_hp, enemy_hp}; next_view.energy = 3; next_view.turn = 1; next_view.burning_blood = config.burning_blood;
        for (const auto& card : deck) {
            auto handle = next_view.cards.create({card.definition, card.upgraded, next_view.next_creation_order++, card.deck_instance}, limits_.cards, config.card_generation);
            if (!handle.present()) return {StepStatus::resource_limit, state.boundary, Restriction::capacity};
            next_view.zone(Zone::draw).push_back(handle);
        }
        next_view.rng.get(RunStream::shuffle).shuffle<CardHandle>(next_view.zone(Zone::draw));
        draw(next, 5, rules_); next_view.phase = Phase::play; next_view.boundary.value = 1;
    } catch (const std::bad_alloc&) { return {StepStatus::resource_limit, state.boundary, Restriction::capacity}; } catch (const std::length_error&) { return {StepStatus::resource_limit, state.boundary, Restriction::capacity}; }
    state.replace(std::move(next)); scratch.steps = 0;
    return result(state);
}
Restriction Simulator::validate(ConstStateView state, const Input& input) const noexcept {
    if (!valid_record_storage(state.record, state.overflow) ||
        state.phase == Phase::uninitialized || state.phase == Phase::invalid || state.rules != rules_revision)
        return Restriction::invalid_state;
    if (input.expected_boundary != state.boundary) return Restriction::stale_boundary;
    if (state.phase == Phase::combat_won || state.phase == Phase::player_died) return Restriction::wrong_phase;
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
    const auto hand = state.zone(Zone::hand);
    if (!card || std::find(hand.begin(), hand.end(), input.card) == hand.end()) return Restriction::stale_card;
    if (input.kind == InputKind::select_card) return Restriction::none;
    const auto* definition = rules_.card(card->definition);
    if (!definition) return Restriction::invalid_state;
    if (definition->enemy_target != input.target_enemy || (input.target_enemy && state.enemy.hp <= 0)) return Restriction::invalid_target;
    if (state.energy < definition->cost) return Restriction::insufficient_energy;
    return Restriction::none;
}
StepResult Simulator::advance(StateView state, Scratch& scratch) const noexcept try {
    if (!valid_record_storage(state.record, state.overflow)) return fail(state, Restriction::invalid_state);
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
        case Opcode::damage: {
            const auto hit = damage(state.enemy, amount, state.player);
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
            move_card(state, Zone::play, Zone::discard, card);
            if (state.enemy.hp == 0) { end_combat(state, true); return result(state); }
            history(state, HistoryKind::play_finished, false, false, card, play); break;
        }
        }
    }
    return result(state);
} catch (const std::bad_alloc&) { state.phase = Phase::invalid; return {StepStatus::resource_limit, state.boundary, Restriction::capacity}; } catch (const std::length_error&) { state.phase = Phase::invalid; return {StepStatus::resource_limit, state.boundary, Restriction::capacity}; }
catch (...) { return fail(state, Restriction::invalid_state); }
StepResult Simulator::apply(StateView state, const Input& input, Scratch& scratch) const noexcept try {
    const auto restriction = validate(state, input);
    if (restriction != Restriction::none) return result(state, restriction);
    if (state.boundary.value == std::numeric_limits<std::uint32_t>::max()) return fail(state, Restriction::capacity);
    if (!state.clock.advance_action()) return fail(state, Restriction::clock_exhausted);
    scratch.steps = 0;
    if (input.kind == InputKind::end_turn) {
        if (!consume(scratch)) return fail(state, Restriction::execution_budget);
        auto hand = state.zone(Zone::hand); auto discard = state.zone(Zone::discard);
        discard.insert(discard.end(), hand.begin(), hand.end()); hand.clear();
        state.enemy.block = 0;
        for (auto& p : state.powers) if (p.active && p.enemy_owner) p.amount_on_turn_start = p.amount;
        switch (state.move) {
        case BeetleMove::shrink: state.player.shrink = true; (void)apply_power(state, PowerId::shrink, -1, false, true); state.move = BeetleMove::chomp; break;
        case BeetleMove::chomp: case BeetleMove::stomp: {
            const auto hit = damage(state.player, state.move == BeetleMove::chomp ? 7 : 13, state.enemy);
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
        state.execution.push_back({input.card, 0, play_id});
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
} catch (const std::length_error&) {
    state.phase = Phase::invalid;
    return {StepStatus::resource_limit, state.boundary, Restriction::capacity};
} catch (...) { return fail(state, Restriction::invalid_state); }
bool valid_state(ConstStateView state, const Ruleset& rules) noexcept {
    if (!valid_record_storage(state.record, state.overflow) || !state.cards.valid()) return false;
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
    if (state.phase == Phase::discard_choice) {
        if (state.execution.size() != 1 || state.zone(Zone::play).size() != 1 || state.zone(Zone::hand).empty()) return false;
        const auto& frame = state.execution.back(); const auto* card = state.cards.get(frame.source);
        if (!card || card->definition != CardId::survivor || frame.instruction != 2 || state.zone(Zone::play)[0] != frame.source) return false;
    } else if (!state.execution.empty() || !state.zone(Zone::play).empty()) return false;
    if (state.phase == Phase::combat_won && (!state.history.empty() || !state.plays.empty() || !state.powers.empty())) return false;
    for (const auto& play : state.plays) if (!state.cards.get(play.card) || play.energy < 0) return false;
    for (const auto& p : state.powers) if (static_cast<unsigned>(p.definition) > 1) return false;
    for (const auto& entry : state.history) {
        if (static_cast<unsigned>(entry.kind) > static_cast<unsigned>(HistoryKind::power_received) || entry.turn < 1 || entry.turn > state.turn) return false;
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
    const auto zones = state.zone_views();
    for (std::size_t a = 0; a < zones.size(); ++a) {
        const auto& zone = zones[a]; membership += zone.size();
        if (state.phase == Phase::combat_won && !zone.empty()) return false;
        for (std::size_t i = 0; i < zone.size(); ++i) {
            if (!state.cards.get(zone[i])) return false;
            for (std::size_t b = 0; b <= a; ++b) {
                const auto end = b == a ? i : zones[b].size();
                for (std::size_t j = 0; j < end; ++j) if (zone[i] == zones[b][j]) return false;
            }
        }
    }
    if (state.phase == Phase::combat_won) return membership == 0;
    if (membership > live) return false;
    for (std::size_t i = 0; i < state.cards.slots().size(); ++i) {
        const auto& slot = state.cards.slots()[i];
        if (!slot.live) continue;
        const CardHandle card{static_cast<std::uint32_t>(i), slot.generation};
        bool referenced = false;
        for (const auto& zone : zones) if (std::find(zone.begin(), zone.end(), card) != zone.end()) referenced = true;
        for (const auto& entry : state.history) if (entry.card == card) referenced = true;
        for (const auto& play : state.plays) if (play.card == card) referenced = true;
        for (const auto& frame : state.execution) if (frame.source == card) referenced = true;
        if (!referenced) return false;
    }
    return true;
}
} // namespace sts2::engine
