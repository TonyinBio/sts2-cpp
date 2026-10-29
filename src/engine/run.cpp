#include "card_state.hpp"
#include "sts2/engine/run.hpp"
#include "sts2/engine/rewards.hpp"
#include "run_flow.hpp"
#include <algorithm>
#include <limits>
#include <new>

namespace sts2::engine {
using namespace flow;
// Internal population helper; initialize_run publishes only after all setup succeeds.
StepResult initialize_run_inventory(RunView, const SeededConfig&);
namespace {
// Prepare combat without publishing run changes. Callers own clock advancement,
// boundary publication and encounter visitation; initialization reads run RNG.
StepResult prepare_combat(CombatView destination, ConstRunView run, Scratch& scratch, BoundaryId rejection_boundary, std::int32_t enemy_hp = 0) {
    std::vector<CardSpec> deck;
    for (std::size_t card_index = 0; card_index < run.deck.size(); ++card_index) {
        const auto& card = run.deck[card_index];
        CardId definition = CardId::count;
        for (unsigned i = 0; i < static_cast<unsigned>(CardId::count); ++i)
            if (Ruleset::pinned().card(static_cast<CardId>(i))->model_id == SetupRules::name(card.definition)) definition = static_cast<CardId>(i);
        if (definition == CardId::count || card.upgrade > 1)
            return {StepStatus::unsupported_content, rejection_boundary, Restriction::unsupported_setup};
        deck.push_back({definition, card.upgrade == 1, run.deck.handles()[card_index]});
    }
    CombatConfig config;
    std::array<ExhaustRelic, 2> listeners {}; std::size_t count = 0;
    config.burning_blood = false;
    for (auto relic : run.relics) {
        const auto name = SetupRules::name(relic);
        if (name == "RELIC.BURNING_BLOOD") config.burning_blood = true;
        if (name == "RELIC.CHARONS_ASHES" || name == "RELIC.JOSS_PAPER") {
            if (count == listeners.size()) return {StepStatus::unsupported_content, rejection_boundary, Restriction::unsupported_setup};
            listeners[count++] = name == "RELIC.CHARONS_ASHES" ? ExhaustRelic::charons_ashes : ExhaustRelic::joss_paper;
        }
    }
    config.exhaust_relics = std::span(listeners).first(count); config.joss_paper_counter = run.joss_paper_counter;
    config.seed = run.seed; config.clock = run.clock.config(); config.initial_rng = &run.rng;
    config.deck = deck; config.player_hp = run.hp; config.player_max_hp = run.max_hp;
    config.card_generation = next_combat_generation(run);
    const auto result = initialize_combat(destination, config, scratch);
    if (result.status == StepStatus::awaiting_input && enemy_hp != 0) {
        if (enemy_hp < 1 || enemy_hp > 40) return {StepStatus::unsupported_content, rejection_boundary, Restriction::unsupported_setup};
        destination.enemy.hp = enemy_hp; destination.enemy.max_hp = enemy_hp;
    }
    return result;
}
std::array<ContentId, 3> neow_options(GameRng& rng) {
    const auto curses = SetupRules::neow_curses(); int index;
    (void)rng.next_int(0, static_cast<int>(curses.size()), index);
    const auto curse = curses[static_cast<std::size_t>(index)]; const auto name = SetupRules::name(curse);
    std::vector<ContentId> positive(SetupRules::neow_positive().begin(), SetupRules::neow_positive().end());
    std::erase_if(positive, [&](auto id) {
        const auto p = SetupRules::name(id);
        return p == "RELIC.MASSIVE_SCROLL" ||
            (name == "RELIC.CURSED_PEARL" && p == "RELIC.GOLDEN_PEARL") ||
            (name == "RELIC.HEFTY_TABLET" && p == "RELIC.ARCANE_SCROLL") ||
            (name == "RELIC.LEAFY_POULTICE" && p == "RELIC.NEW_LEAF") ||
            (name == "RELIC.PRECARIOUS_SHEARS" && p == "RELIC.PRECISE_SCISSORS") ||
            (name == "RELIC.NEOWS_SACRIFICE" && (p == "RELIC.PHIAL_HOLSTER" || p == "RELIC.LOST_COFFER"));
    });
    if (name != "RELIC.LARGE_CAPSULE") positive.push_back(SetupRules::find(rng.next_bool() ? "RELIC.LAVA_ROCK" : "RELIC.SMALL_CAPSULE"));
    positive.push_back(SetupRules::find(rng.next_bool() ? "RELIC.NUTRITIOUS_OYSTER" : "RELIC.STONE_HUMIDIFIER"));
    positive.push_back(SetupRules::find(rng.next_bool() ? "RELIC.NEOWS_TALISMAN" : "RELIC.POMANDER"));
    rng.shuffle<ContentId>(positive);
    return {positive[0], positive[1], curse};
}
}
StepResult initialize_run(RunState& destination, const SeededConfig& config) {
    try {
        RunState state(destination.overflow.allocator()); auto state_view = state.view();
        const auto setup = initialize_run_inventory(state_view, config);
        if (setup.status != StepStatus::awaiting_input) return setup;
        if (!state_view.clock.initialize(config.clock)) return {StepStatus::unsupported_content, {}, Restriction::unsupported_setup};
        MapState map;
        if (!generate_map(map, config.acts[0], state_view.numeric_seed, config.ascension))
            return {StepStatus::unsupported_content, {}, Restriction::unsupported_setup};
        state_view.current = map.start; state_view.visited.push_back(map.start); state_view.maps[0] = std::move(map);
        state_view.hp = config.ascension >= 2 ? static_cast<int>(std::int64_t(state_view.max_hp) * 80 / 100) : state_view.max_hp;
        state_view.acts[0].events_visited = 1; // Native RoomSet.MarkVisited counts the ancient as an event room.
        state_view.event_rng = GameRng(state_view.numeric_seed + game_hash("NEOW"));
        state_view.neow_options = neow_options(state_view.event_rng);
        if (std::find(state_view.neow_options.begin(), state_view.neow_options.end(), no_content) != state_view.neow_options.end())
            return {StepStatus::engine_error, {}, Restriction::invalid_state};
        state_view.phase = RunPhase::neow; state_view.boundary.value = 1;
        destination = std::move(state);
        return {StepStatus::awaiting_input, destination.view().boundary};
    } catch (const std::bad_alloc&) { return {StepStatus::resource_limit, {}, Restriction::capacity}; }
    catch (...) { return {StepStatus::engine_error, {}, Restriction::execution_budget}; }
}
Restriction detail::upgrade_card_permanently(RunState& state, CardHandle handle) noexcept {
    if (!valid_run(state)) return Restriction::invalid_state;
    auto run = state.view();
    if (run.phase != RunPhase::combat || !run.combat) return Restriction::wrong_phase;
    auto* instance = run.combat->cards.get(handle);
    if (!instance || !instance->origin.present()) return Restriction::stale_card;
    auto* permanent = run.deck.get(instance->origin);
    if (!permanent) return Restriction::stale_card;
    permanent->upgrade = 1;
    instance->upgraded = true;
    return Restriction::none;
}
StepResult initialize_seeded_combat(RunState& destination, const SeededConfig& config, CombatTrace trace) {
    const auto result = initialize_combat_scenario(destination, config, {}, trace);
    if (result.status == StepStatus::awaiting_input) destination.view().direct_combat = true;
    return result;
}
StepResult initialize_combat_scenario(RunState& destination, const SeededConfig& config, std::span<const CardSpec> scenario_deck, CombatTrace trace, std::span<const ContentId> scenario_relics, std::int32_t enemy_hp, std::uint32_t joss_counter) {
    if (config.character != "CHARACTER.IRONCLAD" || config.ascension != 0)
        return {StepStatus::unsupported_content, destination.view().boundary, Restriction::unsupported_setup};
    try {
        RunState next(destination.overflow.allocator()); auto next_view = next.view();
        const auto result = initialize_run(next, config);
        if (result.status != StepStatus::awaiting_input) return result;
        if (!scenario_deck.empty()) {
            if (scenario_deck.size() > 4096) return {StepStatus::resource_limit, destination.view().boundary, Restriction::capacity};
            next_view.deck.clear();
            for (const auto& card : scenario_deck) {
                const auto* definition = Ruleset::pinned().card(card.definition);
                if (!definition) return {StepStatus::unsupported_content, destination.view().boundary, Restriction::unsupported_setup};
                const auto id = SetupRules::find(definition->model_id);
                if (id == no_content) return {StepStatus::unsupported_content, destination.view().boundary, Restriction::unsupported_setup};
                next_view.deck.push_back({id, static_cast<std::uint32_t>(next_view.deck.size()), static_cast<std::uint32_t>(card.upgraded), -1});
            }
        }
        if (!scenario_relics.empty()) { next_view.relics.clear(); next_view.relics.insert(next_view.relics.end(), scenario_relics.begin(), scenario_relics.end()); }
        next_view.joss_paper_counter = joss_counter;
        if (!supported_combat_inventory(next_view)) return {StepStatus::unsupported_content, destination.view().boundary, Restriction::unsupported_setup};
        next_view.combat.emplace();
        Scratch scratch; scratch.trace = trace;
        const auto initialized = prepare_combat(*next_view.combat, next_view, scratch, destination.view().boundary, enemy_hp);
        if (initialized.status != StepStatus::awaiting_input) return initialized;
        next_view.acts[0].normal_visited = 1;
        next_view.phase = RunPhase::combat; next_view.direct_combat = false; next_view.combat_generation = 1;
        destination = std::move(next); return initialized;
    } catch (const std::bad_alloc&) { return {StepStatus::resource_limit, destination.view().boundary, Restriction::capacity}; } catch (const std::length_error&) { return {StepStatus::resource_limit, destination.view().boundary, Restriction::capacity}; }
}
StepResult enter_combat_scenario(RunState& state, BoundaryId boundary, std::string_view encounter, CombatTrace trace) {
    if (!valid_run(state)) return {StepStatus::engine_error, state.data.boundary, Restriction::invalid_state};
    auto state_view = state.view();
    if (boundary != state_view.boundary) return {StepStatus::invalid_action, state_view.boundary, Restriction::stale_boundary};
    if (state_view.phase != RunPhase::map || state_view.combat_generation == 0 || (state_view.rewards && !state_view.rewards->resolved))
        return {StepStatus::invalid_action, state_view.boundary, Restriction::wrong_phase};
    if (encounter != "ENCOUNTER.SHRINKER_BEETLE_WEAK" || state_view.ascension != 0 ||
        SetupRules::name(state_view.character) != "CHARACTER.IRONCLAD")
        return {StepStatus::unsupported_content, state_view.boundary, Restriction::unsupported_setup};
    if (!supported_combat_inventory(state_view))
        return {StepStatus::unsupported_content, state_view.boundary, Restriction::unsupported_setup};
    if (next_combat_generation(state_view) == 0 || state_view.acts[state_view.act_index].normal_visited >= state_view.acts[state_view.act_index].normal.size()) return {StepStatus::resource_limit, state_view.boundary, Restriction::capacity};
    try {
        RunState next = state; auto next_view = next.view();
        CombatState combat;
        Scratch scratch; scratch.trace = trace;
        const auto result = prepare_combat(combat, next_view, scratch, state_view.boundary);
        if (result.status != StepStatus::awaiting_input) return result;
        next_view.rewards.reset();
        next_view.combat_generation = next_combat_generation(next_view);
        const auto run_clock = next_view.clock; const auto run_boundary = next_view.boundary;
        next_view.combat = std::move(combat);
        // Scenario entry is not a player action and does not consume the clock.
        next_view.clock = run_clock; next_view.boundary = run_boundary;
        trace.emit(*next_view.combat, {CombatEventKind::combat_updated});
        ++next_view.acts[next_view.act_index].normal_visited;
        next_view.phase = RunPhase::combat; next_view.direct_combat = false;
        state = std::move(next);
        return {StepStatus::awaiting_input, state_view.boundary};
    } catch (const std::bad_alloc&) { return {StepStatus::resource_limit, state_view.boundary, Restriction::capacity}; } catch (const std::length_error&) { return {StepStatus::resource_limit, state_view.boundary, Restriction::capacity}; }
}
namespace {
StepResult apply_combat_transition(RunState& state, const CombatInput& input, Scratch& scratch) {
    auto state_view = state.view();
    if (state_view.phase != RunPhase::combat || !state_view.combat)
        return {StepStatus::invalid_action, state_view.boundary, Restriction::wrong_phase};
    const auto result = apply_combat(*state_view.combat, input, scratch);
    if (result.status == StepStatus::invalid_action || result.status == StepStatus::unsupported_content) return result;
    if (result.status == StepStatus::combat_won) state_view.phase = RunPhase::combat_done;
    else if (result.status == StepStatus::player_died) state_view.phase = RunPhase::complete;
    else if (result.status == StepStatus::engine_error || result.status == StepStatus::resource_limit) state_view.phase = RunPhase::invalid;
    if (!state_view.direct_combat && (result.status == StepStatus::combat_won || result.status == StepStatus::player_died)) {
        child_combat_finished(state_view, result.status == StepStatus::combat_won);
        state_view.combat_generation = highest_combat_generation(state_view);
        state_view.combat.reset();
        *state_view.creatures.get(state_view.data.player) = {state_view.hp, state_view.max_hp};
    }
    return result;
}
StepResult apply_run_transition(RunState& state, const RunInput& input, Scratch& scratch) {
    auto state_view = state.view();
    RunActionPlan plan;
    const auto checked = plan_run_input(state_view, input, plan, ValidationMode::execution);
    if (checked.status != StepStatus::awaiting_input) return checked;
    const auto entering_generation = plan.entering_generation;
    const auto generate_rewards = plan.generate_rewards;
    const auto destination = plan.destination;
    const auto reward_kind = plan.reward_kind;
    std::optional<CombatState> entering_combat;
    if (destination != no_node) {
        try {
            entering_combat.emplace();
            const auto initialized = prepare_combat(*entering_combat, state_view, scratch, state_view.boundary);
            if (initialized.status != StepStatus::awaiting_input) return initialized;
        } catch (const std::bad_alloc&) { return {StepStatus::resource_limit, state_view.boundary, Restriction::capacity}; }
          catch (const std::length_error&) { return {StepStatus::resource_limit, state_view.boundary, Restriction::capacity}; }
    }
    if (state_view.boundary.value == UINT32_MAX) { state_view.phase = RunPhase::invalid; return {StepStatus::resource_limit, state_view.boundary, Restriction::capacity}; }
    try {
        // All validation precedes mutation. Prepare owning growth before time/payment.
        if (input.kind == RunInputKind::choose_option && state_view.phase == RunPhase::neow) state_view.relics.reserve(state_view.relics.size() + 1);
        if (entering_combat) {
            state_view.visited.reserve(state_view.visited.size() + 1);
        }
        if (input.kind == RunInputKind::choose_reward_card) state_view.deck.reserve(state_view.deck.size() + 1);
        if (input.kind == RunInputKind::take_reward) state_view.potions.reserve(static_cast<std::size_t>(state_view.potion_slots));
        // Preflight time before reward generation mutates run RNG/odds. Generation
        // itself stages all fallible work locally and commits only on success.
        auto clock = state_view.clock;
        if (!clock.advance_action()) { state_view.phase = RunPhase::invalid; return {StepStatus::engine_error, state_view.boundary, Restriction::clock_exhausted}; }
        if (generate_rewards) {
            const auto generated = generate_combat_rewards(state_view);
            if (generated.status != StepStatus::awaiting_input)
                return {generated.status, state_view.boundary, generated.reason};
        }
        state_view.clock = clock;
        if (reward_kind) {
            apply_reward_input(state_view, input, plan);
        } else if (input.kind == RunInputKind::discard_potion) {
            state_view.potions[input.option] = no_content;
        } else if (input.kind == RunInputKind::choose_option) {
            apply_room_option(state_view, input);
        } else if (generate_rewards) {
            state_view.phase = RunPhase::rewards;
        } else if (entering_combat) {
            state_view.rewards.reset();
            state_view.combat_generation = entering_generation;
            state_view.current = destination; state_view.visited.push_back(destination);
            ++state_view.acts[state_view.act_index].normal_visited;
            const auto run_clock = state_view.clock; const auto run_boundary = state_view.boundary;
            state_view.combat = std::move(entering_combat);
            state_view.clock = run_clock; state_view.boundary = run_boundary;
            state_view.phase = RunPhase::combat;
        } else if (!return_from_room(state_view)) {
            state_view.phase = RunPhase::invalid;
            return {StepStatus::engine_error, state_view.boundary, Restriction::invalid_state};
        }
        ++state_view.boundary.value;
        if (state_view.phase == RunPhase::combat) {
            scratch.trace.emit(*state_view.combat, {CombatEventKind::combat_updated});
        }
        return {StepStatus::awaiting_input, state_view.boundary};
    } catch (const std::bad_alloc&) { state_view.phase = RunPhase::invalid; return {StepStatus::resource_limit, state_view.boundary, Restriction::capacity}; } catch (const std::length_error&) { state_view.phase = RunPhase::invalid; return {StepStatus::resource_limit, state_view.boundary, Restriction::capacity}; }
}
} // namespace
StepResult apply_input(RunState& state, const PlayerInput& input, Scratch& scratch) {
    if (!valid_creature_storage(state.data, state.overflow) || !valid_room_context(state.view())) return {StepStatus::engine_error, state.data.boundary, Restriction::invalid_state};
    if (const auto* run = std::get_if<RunInput>(&input)) return apply_run_transition(state, *run, scratch);
    return apply_combat_transition(state, std::get<CombatInput>(input), scratch);
}
} // namespace sts2::engine
