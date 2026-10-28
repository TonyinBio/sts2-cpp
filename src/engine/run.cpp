#include "sts2/engine/run.hpp"
#include "sts2/engine/rewards.hpp"
#include <algorithm>
#include <limits>
#include <new>

namespace sts2::engine {
// Internal population helper; initialize_run publishes only after all setup succeeds.
StepResult initialize_run_inventory(RunView, const SeededConfig&);
namespace {
std::uint32_t highest_combat_generation(ConstRunView state) noexcept {
    auto highest = state.combat_generation;
    if (state.combat) highest = std::max(highest, state.combat->cards.data.highest_generation);
    return highest;
}
std::uint32_t next_combat_generation(ConstRunView state) noexcept {
    const auto highest = highest_combat_generation(state);
    return highest == UINT32_MAX ? 0 : highest + 1;
}
bool supported_combat_inventory(ConstRunView state) {
    for (auto potion : state.potions)
        if (potion != no_content && SetupRules::name(potion) != "POTION.POTION_OF_BINDING") return false;
    // Inventory hooks are supported only for the starting relic and Golden Pearl.
    for (auto relic : state.relics)
        if (SetupRules::name(relic) != "RELIC.BURNING_BLOOD" && SetupRules::name(relic) != "RELIC.GOLDEN_PEARL") return false;
    return true;
}
// Prepare combat without publishing run changes. Callers own clock advancement,
// boundary publication and encounter visitation; initialization reads run RNG.
StepResult prepare_combat(CombatView destination, ConstRunView run, Scratch& scratch, BoundaryId rejection_boundary) {
    std::vector<CardSpec> deck;
    for (const auto& card : run.deck) {
        CardId definition = CardId::count;
        for (unsigned i = 0; i < static_cast<unsigned>(CardId::count); ++i)
            if (Ruleset::pinned().card(static_cast<CardId>(i))->model_id == SetupRules::name(card.definition)) definition = static_cast<CardId>(i);
        if (definition == CardId::count || card.upgrade > 1)
            return {StepStatus::unsupported_content, rejection_boundary, Restriction::unsupported_setup};
        deck.push_back({definition, card.upgrade == 1, card.instance});
    }
    CombatConfig config;
    config.seed = run.seed; config.clock = run.clock.config(); config.initial_rng = &run.rng;
    config.deck = deck; config.player_hp = run.hp; config.player_max_hp = run.max_hp;
    config.card_generation = next_combat_generation(run);
    return initialize_combat(destination, config, scratch);
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
StepResult initialize_seeded_combat(RunState& destination, const SeededConfig& config) {
    if (config.character != "CHARACTER.IRONCLAD" || config.ascension != 0)
        return {StepStatus::unsupported_content, destination.view().boundary, Restriction::unsupported_setup};
    try {
        RunState next(destination.overflow.allocator()); auto next_view = next.view();
        const auto result = initialize_run(next, config);
        if (result.status != StepStatus::awaiting_input) return result;
        next_view.combat.emplace();
        Scratch scratch;
        const auto initialized = prepare_combat(*next_view.combat, next_view, scratch, destination.view().boundary);
        if (initialized.status != StepStatus::awaiting_input) return initialized;
        next_view.rng = next_view.combat->rng; next_view.acts[0].normal_visited = 1;
        next_view.phase = RunPhase::combat; next_view.direct_combat = true; next_view.combat_generation = 1;
        destination = std::move(next); return initialized;
    } catch (const std::bad_alloc&) { return {StepStatus::resource_limit, destination.view().boundary, Restriction::capacity}; } catch (const std::length_error&) { return {StepStatus::resource_limit, destination.view().boundary, Restriction::capacity}; }
}
StepResult enter_combat_scenario(RunState& state, BoundaryId boundary, std::string_view encounter) {
    auto state_view = state.view();
    if (!valid_run(state)) return {StepStatus::engine_error, state_view.boundary, Restriction::invalid_state};
    if (boundary != state_view.boundary) return {StepStatus::invalid_action, state_view.boundary, Restriction::stale_boundary};
    if (state_view.phase != RunPhase::map || state_view.combat_generation == 0 || !state_view.rewards || !state_view.rewards->resolved)
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
        Scratch scratch;
        const auto result = prepare_combat(combat, next_view, scratch, state_view.boundary);
        if (result.status != StepStatus::awaiting_input) return result;
        next_view.rewards.reset();
        next_view.combat_generation = next_combat_generation(next_view);
        next_view.combat = std::move(combat); next_view.rng = next_view.combat->rng;
        // Scenario entry is not a player action and does not consume the clock.
        next_view.combat->clock = next_view.clock; next_view.combat->boundary = next_view.boundary;
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
    state_view.clock = state_view.combat->clock; state_view.boundary = state_view.combat->boundary;
    state_view.rng = state_view.combat->rng; state_view.hp = state_view.combat->player.hp;
    if (result.status == StepStatus::combat_won) state_view.phase = RunPhase::combat_done;
    else if (result.status == StepStatus::player_died) state_view.phase = RunPhase::complete;
    else if (result.status == StepStatus::engine_error || result.status == StepStatus::resource_limit) state_view.phase = RunPhase::invalid;
    if (!state_view.direct_combat && (result.status == StepStatus::combat_won || result.status == StepStatus::player_died)) {
        state_view.combat_generation = highest_combat_generation(state_view);
        state_view.combat.reset();
    }
    return result;
}
StepResult apply_run_transition(RunState& state, const RunInput& input) {
    auto state_view = state.view();
    if (state_view.phase == RunPhase::uninitialized || state_view.phase == RunPhase::invalid)
        return {StepStatus::engine_error, state_view.boundary, Restriction::invalid_state};
    if (input.expected_boundary != state_view.boundary) return {StepStatus::invalid_action, state_view.boundary, Restriction::stale_boundary};
    std::optional<CombatState> entering_combat;
    std::uint32_t entering_generation = 0;
    bool generate_rewards = false;
    MapNodeId destination = no_node;
    std::size_t potion_slot = state_view.potions.size();
    std::uint32_t next_card_instance = 0;
    const auto reward_kind = input.kind == RunInputKind::take_reward || input.kind == RunInputKind::skip_rewards ||
        input.kind == RunInputKind::choose_reward_card || input.kind == RunInputKind::reward_alternative;
    if (reward_kind) {
        if (state_view.phase != RunPhase::rewards || !state_view.rewards || state_view.rewards->resolved)
            return {StepStatus::invalid_action, state_view.boundary, Restriction::wrong_phase};
        const auto& rewards = *state_view.rewards;
        if (rewards.selecting_card) {
            if (input.kind == RunInputKind::choose_reward_card) {
                if (input.option >= rewards.cards.size()) return {StepStatus::invalid_action, state_view.boundary, Restriction::invalid_selection};
                const auto id = SetupRules::name(rewards.cards[input.option].definition);
                if (id != "CARD.TRUE_GRIT" && id != "CARD.JUGGLING" && id != "CARD.RAGE")
                    return {StepStatus::unsupported_content, state_view.boundary, Restriction::unsupported_setup};
                if (state_view.deck.size() >= 4096) return {StepStatus::resource_limit, state_view.boundary, Restriction::capacity};
                for (const auto& existing : state_view.deck) {
                    if (existing.instance == UINT32_MAX) return {StepStatus::resource_limit, state_view.boundary, Restriction::capacity};
                    next_card_instance = std::max(next_card_instance, existing.instance + 1);
                }
            } else if (input.kind != RunInputKind::reward_alternative || input.option != 0)
                return {StepStatus::invalid_action, state_view.boundary, Restriction::invalid_selection};
        } else if (input.kind == RunInputKind::take_reward) {
            const auto card_index = rewards.potion == no_content ? 1u : 2u;
            if (input.option > card_index || (input.option == 0 && rewards.gold_taken) ||
                (input.option == 1 && card_index == 2 && rewards.potion_taken) || (input.option == card_index && rewards.card_taken))
                return {StepStatus::invalid_action, state_view.boundary, Restriction::invalid_selection};
            if (input.option == 0 && state_view.gold > INT32_MAX - rewards.gold)
                return {StepStatus::resource_limit, state_view.boundary, Restriction::capacity};
            if (input.option == 1 && card_index == 2) {
                for (std::size_t i = 0; i < state_view.potions.size(); ++i) if (state_view.potions[i] == no_content) { potion_slot = i; break; }
            }
        } else if (input.kind != RunInputKind::skip_rewards)
            return {StepStatus::invalid_action, state_view.boundary, Restriction::wrong_phase};
    } else if (input.kind == RunInputKind::discard_potion) {
        if (state_view.phase != RunPhase::rewards && state_view.phase != RunPhase::map)
            return {StepStatus::unsupported_content, state_view.boundary, Restriction::unsupported_setup};
        if (state_view.rewards && state_view.rewards->selecting_card) return {StepStatus::invalid_action, state_view.boundary, Restriction::wrong_phase};
        if (input.option >= state_view.potions.size() || state_view.potions[input.option] == no_content)
            return {StepStatus::invalid_action, state_view.boundary, Restriction::invalid_selection};
    } else
    if (input.kind == RunInputKind::choose_option) {
        if (state_view.phase != RunPhase::neow) return {StepStatus::invalid_action, state_view.boundary, Restriction::wrong_phase};
        if (input.option >= state_view.neow_options.size()) return {StepStatus::invalid_action, state_view.boundary, Restriction::invalid_selection};
        if (state_view.gold > INT32_MAX - 150)
            return {StepStatus::resource_limit, state_view.boundary, Restriction::capacity};
        if (SetupRules::name(state_view.neow_options[input.option]) != "RELIC.GOLDEN_PEARL")
            return {StepStatus::unsupported_content, state_view.boundary, Restriction::unsupported_setup};
    } else if (input.kind == RunInputKind::proceed) {
        if (state_view.phase == RunPhase::combat_done && !state_view.direct_combat) {
            generate_rewards = true;
        } else if (state_view.phase != RunPhase::neow_done && !(state_view.phase == RunPhase::rewards && state_view.rewards && state_view.rewards->resolved)) return {StepStatus::invalid_action, state_view.boundary, Restriction::wrong_phase};
    } else if (input.kind == RunInputKind::travel) {
        if (state_view.phase != RunPhase::map) return {StepStatus::invalid_action, state_view.boundary, Restriction::wrong_phase};
        const auto& map = *state_view.maps[state_view.act_index]; const auto next = map.at(input.col, input.row);
        if (next == no_node || !map.nodes[state_view.current].children.contains(next)) return {StepStatus::invalid_action, state_view.boundary, Restriction::invalid_selection};
        if (map.nodes[next].kind != MapKind::monster || state_view.ascension != 0 ||
            SetupRules::name(state_view.character) != "CHARACTER.IRONCLAD")
            return {StepStatus::unsupported_content, state_view.boundary, Restriction::unsupported_setup};
        if (next_combat_generation(state_view) == 0)
            return {StepStatus::resource_limit, state_view.boundary, Restriction::capacity};
        const auto& rooms = state_view.acts[state_view.act_index];
        if (rooms.normal_visited >= rooms.normal.size() || SetupRules::name(rooms.normal[rooms.normal_visited]) != "ENCOUNTER.SHRINKER_BEETLE_WEAK")
            return {StepStatus::unsupported_content, state_view.boundary, Restriction::unsupported_setup};
        if (!supported_combat_inventory(state_view))
            return {StepStatus::unsupported_content, state_view.boundary, Restriction::unsupported_setup};
        try {
            entering_combat.emplace();
            entering_generation = next_combat_generation(state_view);
            Scratch scratch; const auto initialized = prepare_combat(*entering_combat, state_view, scratch, state_view.boundary);
            if (initialized.status != StepStatus::awaiting_input) return initialized;
            destination = next;
        } catch (const std::bad_alloc&) { return {StepStatus::resource_limit, state_view.boundary, Restriction::capacity}; } catch (const std::length_error&) { return {StepStatus::resource_limit, state_view.boundary, Restriction::capacity}; }
    } else return {StepStatus::unsupported_content, state_view.boundary, Restriction::unsupported_setup};
    if (state_view.boundary.value == UINT32_MAX) { state_view.phase = RunPhase::invalid; return {StepStatus::resource_limit, state_view.boundary, Restriction::capacity}; }
    try {
        // All validation precedes mutation. Prepare owning growth before time/payment.
        if (input.kind == RunInputKind::choose_option) state_view.relics.reserve(state_view.relics.size() + 1);
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
            auto& rewards = *state_view.rewards;
            if (input.kind == RunInputKind::take_reward) {
                const auto card_index = rewards.potion == no_content ? 1u : 2u;
                if (input.option == 0) { state_view.gold += rewards.gold; rewards.gold_taken = true; }
                else if (input.option == card_index) rewards.selecting_card = true;
                else if (potion_slot < static_cast<std::size_t>(state_view.potion_slots)) {
                    if (potion_slot == state_view.potions.size()) state_view.potions.push_back(rewards.potion);
                    else state_view.potions[potion_slot] = rewards.potion;
                    rewards.potion_taken = true;
                }
            } else if (input.kind == RunInputKind::choose_reward_card) {
                auto card = rewards.cards[input.option]; card.instance = next_card_instance;
                card.floor_added = static_cast<std::int32_t>(state_view.visited.size()); state_view.deck.push_back(card);
                rewards.selected_card = static_cast<std::uint8_t>(input.option); rewards.card_taken = true; rewards.selecting_card = false;
            } else if (input.kind == RunInputKind::reward_alternative) rewards.selecting_card = false;
            else { rewards.skipped = true; rewards.resolved = true; }
            if (rewards.gold_taken && (rewards.potion == no_content || rewards.potion_taken) && rewards.card_taken) rewards.resolved = true;
        } else if (input.kind == RunInputKind::discard_potion) {
            state_view.potions[input.option] = no_content;
        } else if (input.kind == RunInputKind::choose_option) {
            state_view.selected_neow = state_view.neow_options[input.option];
            state_view.relics.push_back(state_view.selected_neow); state_view.gold += 150;
            state_view.phase = RunPhase::neow_done;
        } else if (generate_rewards) {
            state_view.phase = RunPhase::rewards;
        } else if (entering_combat) {
            state_view.rewards.reset();
            state_view.combat_generation = entering_generation;
            state_view.current = destination; state_view.visited.push_back(destination);
            ++state_view.acts[state_view.act_index].normal_visited;
            state_view.combat = std::move(entering_combat); state_view.rng = state_view.combat->rng;
            state_view.phase = RunPhase::combat;
        } else state_view.phase = RunPhase::map;
        ++state_view.boundary.value;
        if (state_view.phase == RunPhase::combat) { state_view.combat->clock = state_view.clock; state_view.combat->boundary = state_view.boundary; }
        return {StepStatus::awaiting_input, state_view.boundary};
    } catch (const std::bad_alloc&) { state_view.phase = RunPhase::invalid; return {StepStatus::resource_limit, state_view.boundary, Restriction::capacity}; } catch (const std::length_error&) { state_view.phase = RunPhase::invalid; return {StepStatus::resource_limit, state_view.boundary, Restriction::capacity}; }
}
} // namespace
StepResult apply_input(RunState& state, const PlayerInput& input, Scratch& scratch) {
    if (const auto* run = std::get_if<RunInput>(&input)) return apply_run_transition(state, *run);
    return apply_combat_transition(state, std::get<CombatInput>(input), scratch);
}
} // namespace sts2::engine
