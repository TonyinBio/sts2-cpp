#include "sts2/kernel/run_simulator.hpp"
#include "sts2/kernel/rewards.hpp"
#include <algorithm>
#include <limits>
#include <new>

namespace sts2::engine {
namespace {
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
        RunState state;
        const auto setup = initialize_setup(state.setup, config);
        if (setup.status != StepStatus::awaiting_input) return setup;
        if (!state.clock.initialize(config.clock)) return {StepStatus::unsupported_content, {}, Restriction::unsupported_setup};
        MapState map;
        if (!generate_map(map, config.acts[0], state.setup.numeric_seed, config.ascension))
            return {StepStatus::unsupported_content, {}, Restriction::unsupported_setup};
        state.current = map.start; state.visited.push_back(map.start); state.maps[0] = std::move(map);
        state.setup.hp = config.ascension >= 2 ? static_cast<int>(std::int64_t(state.setup.max_hp) * 80 / 100) : state.setup.max_hp;
        state.initial_heal = state.setup.hp;
        state.setup.acts[0].events_visited = 1; // Native RoomSet.MarkVisited counts the ancient as an event room.
        state.event_rng = GameRng(state.setup.numeric_seed + game_hash("NEOW"));
        state.neow_options = neow_options(state.event_rng);
        if (std::find(state.neow_options.begin(), state.neow_options.end(), no_content) != state.neow_options.end())
            return {StepStatus::engine_error, {}, Restriction::invalid_state};
        state.phase = RunPhase::neow; state.boundary.value = 1;
        destination = std::move(state);
        return {StepStatus::awaiting_input, destination.boundary};
    } catch (const std::bad_alloc&) { return {StepStatus::resource_limit, {}, Restriction::capacity}; }
    catch (...) { return {StepStatus::engine_error, {}, Restriction::execution_budget}; }
}
StepResult initialize_seeded_combat(RunState& destination, const SeededConfig& config) {
    if (config.character != "CHARACTER.IRONCLAD" || config.ascension != 0)
        return {StepStatus::unsupported_content, destination.boundary, Restriction::unsupported_setup};
    RunState next;
    const auto result = initialize_run(next, config);
    if (result.status != StepStatus::awaiting_input) return result;
    next.combat.emplace(); CombatConfig combat;
    combat.seed = config.seed; combat.clock = config.clock; combat.initial_rng = &next.setup.rng;
    combat.player_hp = next.setup.hp; combat.player_max_hp = next.setup.max_hp;
    Scratch scratch;
    const auto initialized = Simulator{}.initialize(*next.combat, combat, scratch);
    if (initialized.status != StepStatus::awaiting_input) return initialized;
    next.setup.rng = next.combat->rng; next.setup.acts[0].normal_visited = 1;
    next.phase = RunPhase::combat; next.direct_combat = true;
    destination = std::move(next); return initialized;
}
StepResult apply_combat(RunState& state, const Input& input, Scratch& scratch) {
    if (state.phase != RunPhase::combat || !state.combat)
        return {StepStatus::invalid_action, state.boundary, Restriction::wrong_phase};
    const auto result = Simulator{}.apply(*state.combat, input, scratch);
    if (result.status == StepStatus::invalid_action || result.status == StepStatus::unsupported_content) return result;
    state.clock = state.combat->clock; state.boundary = state.combat->boundary;
    state.setup.rng = state.combat->rng; state.setup.hp = state.combat->player.hp;
    if (result.status == StepStatus::combat_won) state.phase = RunPhase::combat_done;
    else if (result.status == StepStatus::player_died) state.phase = RunPhase::complete;
    else if (result.status == StepStatus::engine_error || result.status == StepStatus::resource_limit) state.phase = RunPhase::invalid;
    return result;
}
StepResult apply_run(RunState& state, const RunInput& input) {
    if (state.phase == RunPhase::uninitialized || state.phase == RunPhase::invalid)
        return {StepStatus::engine_error, state.boundary, Restriction::invalid_state};
    if (input.expected_boundary != state.boundary) return {StepStatus::invalid_action, state.boundary, Restriction::stale_boundary};
    std::optional<State> entering_combat;
    std::optional<CombatRewards> generated_rewards;
    std::optional<SetupState> rewarded_setup;
    MapNodeId destination = no_node;
    std::size_t potion_slot = state.setup.potions.size();
    std::uint32_t next_card_instance = 0;
    const auto reward_kind = input.kind == RunInputKind::take_reward || input.kind == RunInputKind::skip_rewards ||
        input.kind == RunInputKind::choose_reward_card || input.kind == RunInputKind::reward_alternative;
    if (reward_kind) {
        if (state.phase != RunPhase::rewards || !state.rewards || state.rewards->resolved)
            return {StepStatus::invalid_action, state.boundary, Restriction::wrong_phase};
        const auto& rewards = *state.rewards;
        if (rewards.selecting_card) {
            if (input.kind == RunInputKind::choose_reward_card) {
                if (input.option >= rewards.cards.size()) return {StepStatus::invalid_action, state.boundary, Restriction::invalid_selection};
                const auto id = SetupRules::name(rewards.cards[input.option].definition);
                if (id != "CARD.TRUE_GRIT" && id != "CARD.JUGGLING" && id != "CARD.RAGE")
                    return {StepStatus::unsupported_content, state.boundary, Restriction::unsupported_setup};
                if (state.setup.deck.size() >= 4096) return {StepStatus::resource_limit, state.boundary, Restriction::capacity};
                for (const auto& existing : state.setup.deck) {
                    if (existing.instance == UINT32_MAX) return {StepStatus::resource_limit, state.boundary, Restriction::capacity};
                    next_card_instance = std::max(next_card_instance, existing.instance + 1);
                }
            } else if (input.kind != RunInputKind::reward_alternative || input.option != 0)
                return {StepStatus::invalid_action, state.boundary, Restriction::invalid_selection};
        } else if (input.kind == RunInputKind::take_reward) {
            const auto card_index = rewards.potion == no_content ? 1u : 2u;
            if (input.option > card_index || (input.option == 0 && rewards.gold_taken) ||
                (input.option == 1 && card_index == 2 && rewards.potion_taken) || (input.option == card_index && rewards.card_taken))
                return {StepStatus::invalid_action, state.boundary, Restriction::invalid_selection};
            if (input.option == 0 && state.setup.gold > INT32_MAX - rewards.gold)
                return {StepStatus::resource_limit, state.boundary, Restriction::capacity};
            if (input.option == 1 && card_index == 2) {
                for (std::size_t i = 0; i < state.setup.potions.size(); ++i) if (state.setup.potions[i] == no_content) { potion_slot = i; break; }
            }
        } else if (input.kind != RunInputKind::skip_rewards)
            return {StepStatus::invalid_action, state.boundary, Restriction::wrong_phase};
    } else if (input.kind == RunInputKind::discard_potion) {
        if (state.phase != RunPhase::rewards && state.phase != RunPhase::map)
            return {StepStatus::unsupported_content, state.boundary, Restriction::unsupported_setup};
        if (state.rewards && state.rewards->selecting_card) return {StepStatus::invalid_action, state.boundary, Restriction::wrong_phase};
        if (input.option >= state.setup.potions.size() || state.setup.potions[input.option] == no_content)
            return {StepStatus::invalid_action, state.boundary, Restriction::invalid_selection};
    } else
    if (input.kind == RunInputKind::choose_option) {
        if (state.phase != RunPhase::neow) return {StepStatus::invalid_action, state.boundary, Restriction::wrong_phase};
        if (input.option >= state.neow_options.size()) return {StepStatus::invalid_action, state.boundary, Restriction::invalid_selection};
        if (state.setup.gold > INT32_MAX - 150 || state.gold_gained > INT32_MAX - 150)
            return {StepStatus::resource_limit, state.boundary, Restriction::capacity};
        if (SetupRules::name(state.neow_options[input.option]) != "RELIC.GOLDEN_PEARL")
            return {StepStatus::unsupported_content, state.boundary, Restriction::unsupported_setup};
    } else if (input.kind == RunInputKind::proceed) {
        if (state.phase == RunPhase::combat_done && !state.direct_combat) {
            try {
                rewarded_setup = state.setup; generated_rewards.emplace();
                const auto generated = generate_combat_rewards(*rewarded_setup, *generated_rewards, state.act_index);
                if (generated.status != StepStatus::awaiting_input) return {generated.status, state.boundary, generated.reason};
            } catch (const std::bad_alloc&) { return {StepStatus::resource_limit, state.boundary, Restriction::capacity}; }
        } else if (state.phase != RunPhase::neow_done && !(state.phase == RunPhase::rewards && state.rewards && state.rewards->resolved)) return {StepStatus::invalid_action, state.boundary, Restriction::wrong_phase};
    } else if (input.kind == RunInputKind::travel) {
        if (state.phase != RunPhase::map) return {StepStatus::invalid_action, state.boundary, Restriction::wrong_phase};
        const auto& map = *state.maps[state.act_index]; const auto next = map.at(input.col, input.row);
        if (next == no_node || !map.nodes[state.current].children.contains(next)) return {StepStatus::invalid_action, state.boundary, Restriction::invalid_selection};
        if (map.nodes[next].kind != MapKind::monster || state.setup.ascension != 0 ||
            SetupRules::name(state.setup.character) != "CHARACTER.IRONCLAD")
            return {StepStatus::unsupported_content, state.boundary, Restriction::unsupported_setup};
        if (state.combat || std::any_of(state.setup.potions.begin(), state.setup.potions.end(), [](auto id) { return id != no_content; }))
            return {StepStatus::unsupported_content, state.boundary, Restriction::unsupported_setup};
        const auto& rooms = state.setup.acts[state.act_index];
        if (rooms.normal_visited >= rooms.normal.size() || SetupRules::name(rooms.normal[rooms.normal_visited]) != "ENCOUNTER.SHRINKER_BEETLE_WEAK")
            return {StepStatus::unsupported_content, state.boundary, Restriction::unsupported_setup};
        // Inventory hooks are supported only for the starting relic and Golden Pearl.
        for (auto relic : state.setup.relics) if (SetupRules::name(relic) != "RELIC.BURNING_BLOOD" && SetupRules::name(relic) != "RELIC.GOLDEN_PEARL")
            return {StepStatus::unsupported_content, state.boundary, Restriction::unsupported_setup};
        try {
            std::vector<CardSpec> deck;
            for (const auto& card : state.setup.deck) {
                CardId id = CardId::count;
                for (unsigned i = 0; i < static_cast<unsigned>(CardId::count); ++i)
                    if (Ruleset::pinned().card(static_cast<CardId>(i))->model_id == SetupRules::name(card.definition)) id = static_cast<CardId>(i);
                if (id == CardId::count || card.upgrade > 1) return {StepStatus::unsupported_content, state.boundary, Restriction::unsupported_setup};
                deck.push_back({id, card.upgrade == 1});
            }
            entering_combat.emplace(); CombatConfig config;
            config.seed = state.setup.seed; config.clock = state.clock.config(); config.initial_rng = &state.setup.rng;
            config.deck = deck; config.player_hp = state.setup.hp; config.player_max_hp = state.setup.max_hp;
            Scratch scratch; const auto initialized = Simulator{}.initialize(*entering_combat, config, scratch);
            if (initialized.status != StepStatus::awaiting_input) return initialized;
            destination = next;
        } catch (const std::bad_alloc&) { return {StepStatus::resource_limit, state.boundary, Restriction::capacity}; }
    } else return {StepStatus::unsupported_content, state.boundary, Restriction::unsupported_setup};
    if (state.boundary.value == UINT32_MAX) { state.phase = RunPhase::invalid; return {StepStatus::resource_limit, state.boundary, Restriction::capacity}; }
    try {
        // All validation precedes mutation. Prepare owning growth before time/payment.
        if (input.kind == RunInputKind::choose_option) state.setup.relics.reserve(state.setup.relics.size() + 1);
        if (entering_combat) state.visited.reserve(state.visited.size() + 1);
        if (input.kind == RunInputKind::choose_reward_card) state.setup.deck.reserve(state.setup.deck.size() + 1);
        if (input.kind == RunInputKind::take_reward) state.setup.potions.reserve(static_cast<std::size_t>(state.setup.potion_slots));
        if (input.kind == RunInputKind::discard_potion) state.discarded_potions.reserve(state.discarded_potions.size() + 1);
        if (!state.clock.advance_action()) { state.phase = RunPhase::invalid; return {StepStatus::engine_error, state.boundary, Restriction::clock_exhausted}; }
        if (reward_kind) {
            auto& rewards = *state.rewards;
            if (input.kind == RunInputKind::take_reward) {
                const auto card_index = rewards.potion == no_content ? 1u : 2u;
                if (input.option == 0) { state.setup.gold += rewards.gold; rewards.gold_taken = true; }
                else if (input.option == card_index) rewards.selecting_card = true;
                else if (potion_slot < static_cast<std::size_t>(state.setup.potion_slots)) {
                    if (potion_slot == state.setup.potions.size()) state.setup.potions.push_back(rewards.potion);
                    else state.setup.potions[potion_slot] = rewards.potion;
                    rewards.potion_taken = true;
                }
            } else if (input.kind == RunInputKind::choose_reward_card) {
                auto card = rewards.cards[input.option]; card.instance = next_card_instance;
                card.floor_added = static_cast<std::int32_t>(state.visited.size()); state.setup.deck.push_back(card);
                rewards.selected_card = static_cast<std::uint8_t>(input.option); rewards.card_taken = true; rewards.selecting_card = false;
            } else if (input.kind == RunInputKind::reward_alternative) rewards.selecting_card = false;
            else { rewards.skipped = true; rewards.resolved = true; }
            if (rewards.gold_taken && (rewards.potion == no_content || rewards.potion_taken) && rewards.card_taken) rewards.resolved = true;
        } else if (input.kind == RunInputKind::discard_potion) {
            state.discarded_potions.push_back(state.setup.potions[input.option]); state.setup.potions[input.option] = no_content;
        } else if (input.kind == RunInputKind::choose_option) {
            state.selected_neow = state.neow_options[input.option];
            state.setup.relics.push_back(state.selected_neow); state.setup.gold += 150; state.gold_gained += 150;
            state.phase = RunPhase::neow_done;
        } else if (generated_rewards) {
            state.setup = std::move(*rewarded_setup); state.rewards = *generated_rewards; state.phase = RunPhase::rewards;
        } else if (entering_combat) {
            state.current = destination; state.visited.push_back(destination);
            ++state.setup.acts[state.act_index].normal_visited;
            state.combat = std::move(entering_combat); state.setup.rng = state.combat->rng;
            state.phase = RunPhase::combat;
        } else state.phase = RunPhase::map;
        ++state.boundary.value;
        if (state.phase == RunPhase::combat) { state.combat->clock = state.clock; state.combat->boundary = state.boundary; }
        return {StepStatus::awaiting_input, state.boundary};
    } catch (const std::bad_alloc&) { state.phase = RunPhase::invalid; return {StepStatus::resource_limit, state.boundary, Restriction::capacity}; }
}
} // namespace sts2::engine
