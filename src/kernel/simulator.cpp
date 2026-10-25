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
void refresh_power_stat(State& s, bool enemy, PowerId id) {
    const auto found = std::find_if(s.powers.begin(), s.powers.end(), [&](const auto& p) { return p.active && p.enemy_owner == enemy && p.definition == id; });
    const auto value = found == s.powers.end() ? 0 : found->amount;
    auto& creature = enemy ? s.enemy : s.player;
    switch (id) {
    case PowerId::vulnerable: creature.vulnerable = value; break;
    case PowerId::strength: creature.strength = value; break;
    case PowerId::dexterity: creature.dexterity = value; break;
    case PowerId::weak: creature.weak = value; break;
    case PowerId::shrink: creature.shrink = found != s.powers.end(); break;
    case PowerId::artifact: break;
    }
}
std::uint32_t apply_power(State& s, PowerId id, int amount, bool enemy, bool enemy_side, CardHandle source = {}, bool has_applier = true, bool applier_enemy = false, std::uint32_t retained_index = UINT32_MAX) {
    // A killed creature cannot receive the later instructions of Uppercut/Bash.
    if ((enemy ? s.enemy.hp : s.player.hp) <= 0) return UINT32_MAX;
    auto found = std::find_if(s.powers.begin(), s.powers.end(), [&](const auto& p) { return p.active && p.definition == id && p.enemy_owner == enemy; });
    if (retained_index != UINT32_MAX) found = s.powers.begin() + retained_index;
    const bool existing = found != s.powers.end();
    const auto found_index = existing ? static_cast<std::uint32_t>(found - s.powers.begin()) : UINT32_MAX;
    if (!existing && !amount) return UINT32_MAX;
    const auto operation_index = s.power_operations.size();
    PowerAmountOperation captured{}; captured.definition = id; captured.requested = amount; captured.accepted = amount;
    captured.enemy_owner = enemy; captured.has_applier = has_applier; captured.applier_enemy = applier_enemy; captured.source = source;
    s.power_operations.push_back(std::move(captured));
    // Received modifiers capture the current native power order once. Only
    // Artifact contributes among this slice's concrete models.
    auto& operation = s.power_operations[operation_index];
    for (bool candidate_enemy : {false, true})
        for (std::size_t i = 0; i < s.powers.size(); ++i)
            if (s.powers[i].active && s.powers[i].enemy_owner == candidate_enemy)
                operation.received_candidates.push_back(static_cast<std::uint32_t>(i));
    while (operation.cursor < operation.received_candidates.size()) {
        const auto candidate = operation.received_candidates[operation.cursor++];
        const auto& listener = s.powers[candidate];
        const bool debuff = (id == PowerId::strength || id == PowerId::dexterity) ? operation.accepted < 0 :
            id == PowerId::shrink || ((id == PowerId::weak || id == PowerId::vulnerable) && operation.accepted >= 0);
        if (listener.definition == PowerId::artifact && listener.enemy_owner == enemy && debuff) {
            operation.accepted = 0; operation.contributed.push_back(candidate);
        }
    }
    amount = operation.accepted;
    std::uint32_t index = found_index;
    if (!existing && amount != 0) {
        index = static_cast<std::uint32_t>(s.powers.size());
        const bool debuff = id == PowerId::weak || id == PowerId::vulnerable || id == PowerId::shrink;
        PowerRecord power{}; power.definition = id; power.amount = amount; power.enemy_owner = enemy; power.skip_next_tick = !enemy && debuff;
        power.applier_enemy = applier_enemy; power.has_applier = has_applier; power.source = source;
        s.powers.push_back(power);
    } else if (existing) {
        auto& power = s.powers[index];
        power.amount = static_cast<int>(std::clamp(std::int64_t(power.amount) + amount, std::int64_t(-999999999), std::int64_t(999999999)));
    }
    if (existing || amount != 0) {
        refresh_power_stat(s, enemy, id);
        history(s, HistoryKind::power_received, enemy_side, enemy, {}, index, amount);
        s.history.back().has_applier = has_applier; s.history.back().applier_enemy = applier_enemy;
        if (enemy && amount > 0 && (id == PowerId::weak || id == PowerId::vulnerable)) ++s.debuffs_applied;
    }
    // After-modifier callbacks use a fresh eligible pass intersected with the
    // identities which actually contributed; additions cannot inherit a callback.
    const auto contributed = s.power_operations[operation_index].contributed;
    for (std::size_t candidate = 0; candidate < s.powers.size(); ++candidate) {
        if (!s.powers[candidate].active || std::find(contributed.begin(), contributed.end(), candidate) == contributed.end()) continue;
        const auto owner = s.powers[candidate].enemy_owner;
        (void)apply_power(s, PowerId::artifact, -1, owner, enemy_side, {}, false);
    }
    if (index != UINT32_MAX) {
        auto& power = s.powers[index];
        if ((id == PowerId::strength || id == PowerId::dexterity || id == PowerId::shrink) ? power.amount == 0 : power.amount <= 0) power.active = false;
        refresh_power_stat(s, enemy, id);
    }
    s.power_operations.pop_back();
    return index;
}
void capture_duration(State& state, bool enemy_side = true) {
    auto& pass = state.power_dispatch;
    pass.candidates.clear(); pass.cursor = 0; pass.active = true; pass.enemy_side = enemy_side;
    for (bool enemy_owner : {false, true})
        for (std::size_t i = 0; i < state.powers.size(); ++i)
            if (state.powers[i].active && state.powers[i].enemy_owner == enemy_owner)
                pass.candidates.push_back(static_cast<std::uint32_t>(i));
    if (pass.candidates.empty()) pass.active = false;
}
void step_duration(State& state) {
    auto& pass = state.power_dispatch;
    if (!pass.active) return;
    const auto index = pass.candidates[pass.cursor++];
    auto& p = state.powers[index];
    // Removed powers keep their owner association and remain eligible. Their
    // amount changes the retained instance, never a newly stacked replacement.
    if (((p.enemy_owner && state.enemy.hp > 0) || (!p.enemy_owner && state.player.hp > 0)) &&
        (p.definition == PowerId::vulnerable || p.definition == PowerId::weak)) {
        if (p.skip_next_tick) p.skip_next_tick = false;
        else {
            const auto id = p.definition; const auto owner = p.enemy_owner;
            (void)apply_power(state, id, -1, owner, pass.enemy_side, {}, false, false, index);
        }
    }
    if (pass.cursor == pass.candidates.size()) { pass.active = false; pass.candidates.clear(); pass.cursor = 0; }
}
void capture_card_played_listeners(State& state) {
    auto& pass = state.card_played_dispatch;
    pass.candidates.clear(); pass.cursor = 0;
    for (std::size_t i = 0; i < state.powers.size(); ++i)
        if (state.powers[i].active && !state.powers[i].enemy_owner) pass.candidates.push_back({ListenerKind::power, static_cast<std::uint32_t>(i)});
    if (state.player.hp > 0) {
        for (std::size_t i = 0; i < state.relics.size(); ++i)
            if (state.relics[i].active) pass.candidates.push_back({ListenerKind::relic, static_cast<std::uint32_t>(i)});
        for (const auto& zone : state.zones) for (auto card : zone)
            pass.candidates.push_back({ListenerKind::card, card.slot, card.generation});
    }
    for (std::size_t i = 0; i < state.powers.size(); ++i)
        if (state.powers[i].active && state.powers[i].enemy_owner) pass.candidates.push_back({ListenerKind::power, static_cast<std::uint32_t>(i)});
    pass.candidates.push_back({ListenerKind::monster});
}
bool listener_eligible(const State& state, ListenerRef listener) {
    switch (listener.kind) {
    case ListenerKind::power: return state.powers[listener.index].enemy_owner || state.player.hp > 0;
    case ListenerKind::relic: return state.relics[listener.index].active && state.player.hp > 0;
    case ListenerKind::card: {
        const auto* card = state.cards.get({listener.index, listener.generation});
        return card && !card->removed && state.player.hp > 0;
    }
    case ListenerKind::monster: return true;
    }
    return false;
}
void step_card_played(State& state) {
    auto& pass = state.card_played_dispatch;
    if (pass.stage == 0) return;
    const auto listener = pass.candidates[pass.cursor++];
    if (listener_eligible(state, listener)) {
        pass.visited[pass.stage - 1].push_back(listener);
        // The probe perturbs real models, never installs fake listeners. The
        // supported concrete models inherit default AfterCardPlayed callbacks.
        if (pass.perturb && pass.stage == 1 && pass.visited[0].size() == 1) {
            state.powers[pass.remove_power].active = false;
            refresh_power_stat(state, state.powers[pass.remove_power].enemy_owner, state.powers[pass.remove_power].definition);
            for (auto& zone : state.zones) std::erase(zone, pass.remove_card);
            state.cards.get(pass.remove_card)->removed = true;
        }
    }
    if (pass.cursor == pass.candidates.size()) {
        if (pass.stage == 1) {
            if (pass.perturb) (void)apply_power(state, PowerId::strength, 1, false, false);
            pass.stage = 2; capture_card_played_listeners(state);
        } else { pass.stage = 0; pass.cursor = 0; pass.candidates.clear(); }
    }
}
void begin_card_played(State& state, std::uint32_t play, bool perturb = false, CardHandle removed = {}, std::uint32_t power = UINT32_MAX) {
    auto& pass = state.card_played_dispatch;
    pass.play = play; pass.stage = 1; pass.perturb = perturb; pass.remove_card = removed; pass.remove_power = power;
    for (auto& visits : pass.visited) visits.clear();
    capture_card_played_listeners(state);
}
void move_card(State& state, Zone from, Zone to, CardHandle card) {
    auto& source = state.zone(from);
    source.erase(std::find(source.begin(), source.end(), card));
    state.zone(to).push_back(card);
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
    std::int64_t numerator = std::int64_t(std::max(0, base + source.strength)) * (source.shrink ? 70 : 100) * (source.weak > 0 ? 75 : 100) * (target.vulnerable > 0 ? 150 : 100);
    const auto amount = static_cast<int>(numerator / 1000000);
    const auto blocked = std::min(target.block, amount);
    const auto unblocked = std::min(target.hp, amount - blocked);
    const HitResult hit{blocked, unblocked, amount - blocked - unblocked,
        target.block > 0 && blocked == target.block, blocked >= amount, unblocked == target.hp};
    target.block -= blocked; target.hp -= unblocked;
    return hit;
}
void after_card_exhausted(State& state, CardHandle card, const Ruleset& rules) {
    // Ordinary hook guards once at entry; lethal listener damage does not abort
    // the captured pass. Combat cleanup waits until the outer card effect ends.
    if (state.enemy.hp <= 0 || state.player.hp <= 0) return;
    auto& pass = state.exhaust_dispatch;
    pass.card = card; pass.candidates.clear(); pass.cursor = 0; pass.active = true;
    for (std::size_t i = 0; i < state.relics.size(); ++i)
        if (state.relics[i].active) pass.candidates.push_back(static_cast<std::uint32_t>(i));
    while (pass.cursor < pass.candidates.size()) {
        const auto index = pass.candidates[pass.cursor++];
        auto& relic = state.relics[index];
        if (!relic.active || state.player.hp == 0) continue;
        if (relic.definition == RelicId::charons_ashes && state.enemy.hp > 0) {
            CreatureState unpowered;
            auto target = state.enemy; target.vulnerable = 0;
            const auto hit = damage(target, 3, unpowered);
            state.enemy.hp = target.hp; state.enemy.block = target.block;
            state.damage_dealt += hit.unblocked;
            history(state, HistoryKind::damaged, false, true, {}, 0, 0, hit);
            state.history.back().props = 4;
        } else if (relic.definition == RelicId::joss_paper) {
            ++relic.counter;
            if (relic.counter >= 5) {
                if (state.enemy.hp > 0) draw(state, relic.counter / 5, rules);
                relic.counter %= 5;
            }
        }
    }
    pass.active = false; pass.candidates.clear(); pass.cursor = 0; pass.card = {};
}
void end_combat(State& state, bool won) noexcept {
    state.phase = won ? Phase::combat_won : Phase::player_died;
    state.execution.clear(); state.card_played_dispatch = {};
    if (won) {
        state.history.clear(); state.plays.clear(); state.powers.clear();
        for (auto& zone : state.zones) zone.clear();
    } else {
        for (auto& power : state.powers) if (!power.enemy_owner) power.active = false;
    }
    state.player.block = 0; state.player.shrink = false; state.player.vulnerable = 0; state.player.strength = 0; state.player.dexterity = 0; state.player.weak = 0;
    if (won && state.burning_blood) {
        const auto healed = std::min(6, state.player.max_hp - state.player.hp);
        state.player.hp += healed; state.healing += healed;
    }
}
}
StepResult begin_card_played_probe(State& state, BoundaryId boundary, std::uint32_t play, CardHandle removed, std::uint32_t power) noexcept try {
    if (!valid_state(state)) return {StepStatus::engine_error, state.boundary, Restriction::invalid_state};
    if (boundary != state.boundary) return result(state, Restriction::stale_boundary);
    if (state.phase != Phase::play || state.power_dispatch.active || state.card_played_dispatch.stage) return result(state, Restriction::wrong_phase);
    if (play >= state.plays.size() || power >= state.powers.size() || !state.cards.get(removed) || state.cards.get(removed)->removed)
        return result(state, Restriction::malformed_action);
    begin_card_played(state, play, true, removed, power); return result(state);
} catch (...) { return fail(state, Restriction::capacity); }
StepResult step_card_played_probe(State& state, BoundaryId boundary) noexcept try {
    if (!valid_state(state)) return {StepStatus::engine_error, state.boundary, Restriction::invalid_state};
    if (boundary != state.boundary) return result(state, Restriction::stale_boundary);
    if (state.phase != Phase::play || !state.card_played_dispatch.stage) return result(state, Restriction::wrong_phase);
    step_card_played(state); return result(state);
} catch (...) { return fail(state, Restriction::capacity); }
StepResult capture_duration_scenario(State& state, BoundaryId boundary) noexcept try {
    if (!valid_state(state)) return {StepStatus::engine_error, state.boundary, Restriction::invalid_state};
    if (boundary != state.boundary) return result(state, Restriction::stale_boundary);
    if (state.phase != Phase::play || state.power_dispatch.active) return result(state, Restriction::wrong_phase);
    capture_duration(state, false); return result(state);
} catch (...) { return fail(state, Restriction::capacity); }
StepResult step_duration_scenario(State& state, BoundaryId boundary) noexcept try {
    if (!valid_state(state)) return {StepStatus::engine_error, state.boundary, Restriction::invalid_state};
    if (boundary != state.boundary) return result(state, Restriction::stale_boundary);
    if (state.phase != Phase::play || !state.power_dispatch.active) return result(state, Restriction::wrong_phase);
    step_duration(state); return result(state);
} catch (...) { return fail(state, Restriction::capacity); }
StepResult remove_power_scenario(State& state, BoundaryId boundary, std::uint32_t index) noexcept {
    if (!valid_state(state)) return {StepStatus::engine_error, state.boundary, Restriction::invalid_state};
    if (boundary != state.boundary) return result(state, Restriction::stale_boundary);
    if (state.phase != Phase::play) return result(state, Restriction::wrong_phase);
    if (index >= state.powers.size()) return result(state, Restriction::malformed_action);
    auto& power = state.powers[index]; power.active = false;
    auto& owner = power.enemy_owner ? state.enemy : state.player;
    switch (power.definition) {
    case PowerId::strength: owner.strength = 0; break;
    case PowerId::dexterity: owner.dexterity = 0; break;
    case PowerId::vulnerable: owner.vulnerable = 0; break;
    case PowerId::weak: owner.weak = 0; break;
    case PowerId::shrink: owner.shrink = false; break;
    case PowerId::artifact: break;
    }
    return result(state);
}
StepResult apply_power_scenario(State& state, BoundaryId boundary, const PowerScenario& operation) noexcept try {
    if (!valid_state(state)) return {StepStatus::engine_error, state.boundary, Restriction::invalid_state};
    if (boundary != state.boundary) return result(state, Restriction::stale_boundary);
    if (state.phase != Phase::play) return result(state, Restriction::wrong_phase);
    if (static_cast<unsigned>(operation.definition) > static_cast<unsigned>(PowerId::artifact) ||
        operation.amount < -999999999 || operation.amount > 999999999 ||
        (operation.source.present() && !state.cards.get(operation.source))) return result(state, Restriction::malformed_action);
    (void)apply_power(state, operation.definition, operation.amount, operation.enemy_owner, false,
        operation.source, operation.has_applier, operation.applier_enemy);
    return result(state);
} catch (const std::bad_alloc&) {
    state.phase = Phase::invalid; return {StepStatus::resource_limit, state.boundary, Restriction::capacity};
} catch (...) { return fail(state, Restriction::invalid_state); }
void State::reserve(std::size_t card_count) {
    cards.reserve(card_count);
    for (auto& zone : zones) zone.reserve(card_count);
    execution.reserve(8); history.reserve(128); plays.reserve(32); powers.reserve(8); relics.reserve(8); exhaust_dispatch.candidates.reserve(8); power_operations.reserve(8); power_dispatch.candidates.reserve(8);
}
std::size_t State::retained_bytes() const noexcept {
    const auto text_address = reinterpret_cast<std::uintptr_t>(seed.data());
    const auto root_address = reinterpret_cast<std::uintptr_t>(this);
    const auto seed_bytes = text_address >= root_address && text_address < root_address + sizeof(State) ? 0 : seed.capacity() + 1;
    auto bytes = relics.capacity() * sizeof(CombatRelic) + exhaust_dispatch.candidates.capacity() * sizeof(std::uint32_t) + sizeof(State) + seed_bytes + cards.retained_bytes() + execution.capacity() * sizeof(EffectFrame);
    bytes += history.capacity() * sizeof(CombatHistoryEntry) + plays.capacity() * sizeof(CardPlayRecord) + powers.capacity() * sizeof(PowerRecord) + power_operations.capacity() * sizeof(PowerAmountOperation) + power_dispatch.candidates.capacity() * sizeof(std::uint32_t);
    bytes += card_played_dispatch.candidates.capacity() * sizeof(ListenerRef);
    for (const auto& visits : card_played_dispatch.visited) bytes += visits.capacity() * sizeof(ListenerRef);
    for (const auto& operation : power_operations) bytes += (operation.received_candidates.capacity() + operation.contributed.capacity()) * sizeof(std::uint32_t);
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
        if (config.relics.empty()) {
            if (config.burning_blood) next.relics.push_back({RelicId::burning_blood});
        } else {
            next.burning_blood = false;
            for (auto id : config.relics) {
                if (id > RelicId::joss_paper) return {StepStatus::unsupported_content, state.boundary, Restriction::unsupported_setup};
                next.relics.push_back({id});
                if (id == RelicId::burning_blood) next.burning_blood = true;
            }
        }
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
    if (state.power_dispatch.active || state.card_played_dispatch.stage) return Restriction::wrong_phase;
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
    if (state.phase != Phase::play) return result(state);
    while (state.power_dispatch.active) step_duration(state);
    while (state.card_played_dispatch.stage) step_card_played(state);
    if (state.execution.empty()) return result(state);
    while (!state.execution.empty()) {
        if (!consume(scratch)) return fail(state, Restriction::execution_budget);
        auto& frame = state.execution.back(); const auto* source = state.cards.get(frame.source);
        if (!source) return fail(state, Restriction::invalid_state);
        const auto* definition = rules_.card(source->definition);
        if (!definition || frame.instruction >= definition->program.size()) return fail(state, Restriction::invalid_state);
        const auto instruction = definition->program[frame.instruction++];
        int amount = instruction.base + (source->upgraded ? instruction.upgrade : 0);
        switch (instruction.opcode) {
        case Opcode::damage: {
            const auto hit = damage(state.enemy, amount, state.player);
            state.damage_dealt += hit.unblocked;
            history(state, HistoryKind::damaged, false, true, frame.source, 0, 0, hit);
            history(state, HistoryKind::attacked, false, false, frame.source, 0, 0, hit);
            break;
        }
        case Opcode::block:
            amount = std::max(0, amount + state.player.dexterity);
            if (amount > std::numeric_limits<int>::max() - state.player.block) return fail(state, Restriction::capacity);
            state.player.block += amount; history(state, HistoryKind::block_gained, false, false, frame.source, frame.play, amount); break;
        case Opcode::vulnerable:
            (void)apply_power(state, PowerId::vulnerable, amount, true, false, frame.source); break;
        case Opcode::weak:
            (void)apply_power(state, PowerId::weak, amount, true, false, frame.source); break;
        case Opcode::strength:
            (void)apply_power(state, PowerId::strength, amount, false, false, frame.source); break;
        case Opcode::discard_choice:
            if (state.zone(Zone::hand).size() == 1) {
                const auto discarded = state.zone(Zone::hand).front();
                move_card(state, Zone::hand, Zone::discard, discarded);
                history(state, HistoryKind::discarded, false, false, discarded);
            } else if (!state.zone(Zone::hand).empty()) { state.phase = Phase::discard_choice; return result(state); }
            break;
        case Opcode::finish: {
            const auto card = frame.source; const auto play = frame.play; state.execution.pop_back();
            history(state, HistoryKind::play_finished, false, false, card, play);
            begin_card_played(state, play);
            while (state.card_played_dispatch.stage) step_card_played(state);
            const auto id = state.cards.get(card)->definition;
            const auto destination = id == CardId::shockwave || id == CardId::tremble ? Zone::exhaust : Zone::discard;
            if (id != CardId::inflame) move_card(state, Zone::play, destination, card);
            else { state.cards.get(card)->removed = true; state.zone(Zone::play).erase(std::find(state.zone(Zone::play).begin(), state.zone(Zone::play).end(), card)); }
            if (destination == Zone::exhaust) {
                history(state, HistoryKind::exhausted, false, false, card);
                after_card_exhausted(state, card, rules_);
            }
            if (state.enemy.hp == 0) { end_combat(state, true); return result(state); }
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
    if (input.kind == InputKind::end_turn) {
        if (!consume(scratch)) return fail(state, Restriction::execution_budget);
        auto& hand = state.zone(Zone::hand); auto& discard = state.zone(Zone::discard);
        discard.insert(discard.end(), hand.begin(), hand.end()); hand.clear();
        state.enemy.block = 0;
        for (auto& p : state.powers) if (p.active && p.enemy_owner) p.amount_on_turn_start = p.amount;
        switch (state.move) {
        case BeetleMove::shrink: state.player.shrink = true; (void)apply_power(state, PowerId::shrink, -1, false, true, {}, true, true); state.move = BeetleMove::chomp; break;
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
            // Weak and Vulnerable tick at enemy-side end for either owner.
            capture_duration(state);
            while (state.power_dispatch.active) step_duration(state);
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
    if (state.phase == Phase::discard_choice) {
        if (state.execution.size() != 1 || state.zone(Zone::play).size() != 1 || state.zone(Zone::hand).empty()) return false;
        const auto& frame = state.execution.back(); const auto* card = state.cards.get(frame.source);
        if (!card || card->definition != CardId::survivor || frame.instruction != 2 || state.zone(Zone::play)[0] != frame.source) return false;
    } else if (!state.execution.empty() || !state.zone(Zone::play).empty()) return false;
    if (state.phase == Phase::combat_won && (!state.history.empty() || !state.plays.empty() || !state.powers.empty())) return false;
    if (state.damage_dealt < 0 || state.damage_taken < 0 || state.debuffs_applied < 0 || state.healing < 0) return false;
    for (const auto& play : state.plays) if (!state.cards.get(play.card) || play.energy < 0) return false;
    for (const auto& p : state.powers) {
        if (static_cast<unsigned>(p.definition) > static_cast<unsigned>(PowerId::artifact) ||
            p.amount < -999999999 || p.amount > 999999999 ||
            (p.source.present() && !state.cards.get(p.source))) return false;
    }
    const auto valid_listener = [&](ListenerRef listener) {
        switch (listener.kind) {
        case ListenerKind::power: return listener.index < state.powers.size() && listener.generation == 0;
        case ListenerKind::relic: return listener.index < state.relics.size() && listener.generation == 0;
        case ListenerKind::card: return state.cards.get({listener.index, listener.generation}) != nullptr;
        case ListenerKind::monster: return listener.index == 0 && listener.generation == 0;
        }
        return false;
    };
    const auto& card_pass = state.card_played_dispatch;
    if (card_pass.stage > 2 || card_pass.cursor > card_pass.candidates.size() ||
        (card_pass.stage && card_pass.cursor == card_pass.candidates.size()) ||
        (!card_pass.stage && (!card_pass.candidates.empty() || card_pass.cursor)) ||
        (card_pass.play != UINT32_MAX && card_pass.play >= state.plays.size()) ||
        (card_pass.perturb && (!state.cards.get(card_pass.remove_card) || card_pass.remove_power >= state.powers.size()))) return false;
    for (const auto& listener : card_pass.candidates) if (!valid_listener(listener)) return false;
    for (const auto& visits : card_pass.visited) for (const auto& listener : visits) if (!valid_listener(listener)) return false;
    if (!state.power_operations.empty() || state.exhaust_dispatch.active || !state.exhaust_dispatch.candidates.empty() || state.exhaust_dispatch.cursor || state.exhaust_dispatch.card.present()) return false;
    for (const auto& relic : state.relics) if (relic.definition > RelicId::joss_paper || relic.counter < 0 || relic.counter >= 5) return false;
    if (state.power_dispatch.cursor > state.power_dispatch.candidates.size() ||
        (!state.power_dispatch.active && (!state.power_dispatch.candidates.empty() || state.power_dispatch.cursor))) return false;
    for (auto index : state.power_dispatch.candidates) if (index >= state.powers.size()) return false;
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
            if (!state.cards.get(zone[i]) || state.cards.get(zone[i])->removed) return false;
            for (std::size_t b = 0; b <= a; ++b) {
                const auto end = b == a ? i : state.zones[b].size();
                for (std::size_t j = 0; j < end; ++j) if (zone[i] == state.zones[b][j]) return false;
            }
        }
    }
    return state.phase == Phase::combat_won ? membership == 0 : membership + std::count_if(state.cards.slots().begin(), state.cards.slots().end(), [](const auto& slot) { return slot.live && slot.value.removed; }) == live;
}
} // namespace sts2::engine
