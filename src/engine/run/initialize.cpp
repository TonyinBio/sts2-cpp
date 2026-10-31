#include "sts2/engine/run.hpp"
#include "sts2/content/cards.hpp"
#include "../mechanics/cards.hpp"
#include "run_flow.hpp"
#include <algorithm>
#include <new>
#include <stdexcept>

namespace sts2::engine {
using namespace flow;
namespace {
void populate(RelicBag bag, std::span<const RelicEntry> shared, std::span<const RelicEntry> local, bool filter, GameRng& rng) {
    auto add = [&](auto pool) {
        for (const auto& relic : pool) {
            const auto tier = static_cast<std::size_t>(relic.tier);
            if (filter && tier > static_cast<std::size_t>(RelicTier::shop)) continue;
            if (bag.tiers[tier].empty()) bag.order.push_back(relic.tier);
            bag.tiers[tier].push_back(relic.id);
        }
    };
    add(shared); add(local);
    for (auto tier : bag.order) rng.shuffle<ContentId>(bag.tiers[static_cast<std::size_t>(tier)]);
}
ContentId select(std::span<const ContentId> values, GameRng& rng) {
    if (values.empty()) return no_content;
    int index; (void)rng.next_int(0, static_cast<int>(values.size()), index);
    return values[static_cast<std::size_t>(index)];
}
ContentId select(std::span<const EncounterEntry> values, GameRng& rng, ContentId excluded = no_content) {
    std::vector<ContentId> ids;
    for (const auto& value : values) if (value.id != excluded) ids.push_back(value.id);
    return select(ids, rng);
}
void fill_encounters(SequenceView<ContentId, 32> output, std::span<const EncounterEntry> definitions, std::size_t target, GameRng& rng,
                     std::uint64_t& previous_tags) {
    std::vector<EncounterEntry> bag;
    while (output.size() < target) {
        if (bag.empty()) bag.assign(definitions.begin(), definitions.end());
        if (bag.empty()) throw std::invalid_argument("Empty encounter pool");
        const auto previous = output.empty() ? no_content : output.back();
        auto allowed = [&](const auto& e) { return e.id != previous && (e.tags & previous_tags) == 0; };
        const bool filtered = std::any_of(bag.begin(), bag.end(), allowed);
        bool selected = false;
        for (unsigned tries = 0; tries < 100000; ++tries) {
            const auto index = static_cast<std::size_t>(rng.next_double() * static_cast<double>(bag.size()));
            const auto entry = bag[index];
            if (filtered && !allowed(entry)) continue;
            output.push_back(entry.id); previous_tags = entry.tags;
            bag.erase(bag.begin() + static_cast<std::ptrdiff_t>(index)); selected = true; break;
        }
        if (!selected) throw std::runtime_error("Encounter rejection sampler exceeded execution budget");
    }
}
StepResult initialize_run_inventory(RunView state_view, const SeededConfig& config) {
    RunClock clock;
    const auto* character = SetupRules::character(SetupRules::find(config.character));
    std::array<const ActDefinition*, 3> definitions{};
    if (!character || config.build != game_build || config.ascension > 10 || config.seed.empty() || config.seed.size() > 4096 ||
        !clock.initialize(config.clock) || (config.seed.starts_with("old") && std::any_of(config.seed.begin(), config.seed.end(), [](unsigned char c) { return c >= 128; })))
        return {StepStatus::unsupported_content, {}, Restriction::unsupported_setup};
    for (std::size_t i = 0; i < definitions.size(); ++i) {
        definitions[i] = SetupRules::act(SetupRules::find(config.acts[i]));
        if (!definitions[i] || definitions[i]->index != i) return {StepStatus::unsupported_content, {}, Restriction::unsupported_setup};
    }
    try {
        state_view.seed = config.seed; state_view.numeric_seed = game_seed(config.seed);
        state_view.character = character->id; state_view.ascension = config.ascension;
        state_view.hp = state_view.max_hp = character->hp; state_view.gold = character->gold; state_view.orb_slots = character->orb_slots;
        state_view.potion_slots = config.ascension >= 4 ? 2 : 3;
        state_view.rng = RngBank(config.seed); state_view.player_rng = PlayerRngBank(config.seed);
        for (auto id : character->deck) state_view.deck.push_back({id, static_cast<std::uint32_t>(state_view.deck.size()), 0, 1});
        if (config.ascension >= 5) state_view.deck.push_back({SetupRules::find("CARD.ASCENDERS_BANE"), static_cast<std::uint32_t>(state_view.deck.size()), 0, 1});
        state_view.relics.assign(character->relics.begin(), character->relics.end());
        auto& rng = state_view.rng.get(RunStream::up_front);
        populate(state_view.shared_relics, SetupRules::shared_relics(), {}, false, rng);
        populate(state_view.player_relics, SetupRules::shared_relics(), character->relic_pool, true, rng);
        std::vector<ContentId> shared(SetupRules::shared_ancients().begin(), SetupRules::shared_ancients().end()); rng.shuffle<ContentId>(shared);
        for (std::size_t i = 1; i < state_view.acts.size(); ++i) {
            int count; (void)rng.next_int(0, static_cast<int>(shared.size() + 1), count);
            state_view.acts[i].shared_ancients.assign(shared.begin(), shared.begin() + count); shared.erase(shared.begin(), shared.begin() + count);
        }
        for (std::size_t i = 0; i < state_view.acts.size(); ++i) {
            auto act = state_view.acts[i]; const auto& def = *definitions[i]; act.act = def.id;
            act.events.assign(def.events.begin(), def.events.end());
            act.events.insert(act.events.end(), SetupRules::shared_events().begin(), SetupRules::shared_events().end());
            rng.shuffle<ContentId>(act.events);
            std::uint64_t previous_tags = 0;
            fill_encounters(act.normal, def.weak, def.weak_count, rng, previous_tags);
            fill_encounters(act.normal, def.normal, def.rooms, rng, previous_tags);
            previous_tags = 0; fill_encounters(act.elites, def.elite, 15, rng, previous_tags);
            act.boss = select(def.bosses, rng);
            std::vector<ContentId> ancients(def.ancients.begin(), def.ancients.end());
            ancients.insert(ancients.end(), act.shared_ancients.begin(), act.shared_ancients.end());
            act.ancient = select(ancients, rng);
            if (i == state_view.acts.size() - 1 && config.ascension >= 10) act.second_boss = select(def.bosses, rng, act.boss);
        }
        return {StepStatus::awaiting_input, {1}};
    } catch (const std::bad_alloc&) { return {StepStatus::resource_limit, {}, Restriction::capacity}; }
    catch (...) { return {StepStatus::engine_error, {}, Restriction::execution_budget}; }
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
// Build combat from run inventory; callers own clock and boundary publication.
StepResult flow::prepare_combat(CombatView destination, ConstRunView run, Scratch& scratch, BoundaryId rejection_boundary, std::int32_t enemy_hp) {
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
    if (result.status == StepStatus::awaiting_input) {
        for (std::size_t i = 0; i < destination.cards.slots().size(); ++i) {
            const auto& slot = destination.cards.slots()[i];
            if (!slot.live || !slot.value.origin.present()) continue;
            if (const auto* extra = gameplay::find_card_extra(run, slot.value.origin))
                gameplay::set_card_extra(destination, CardHandle{static_cast<std::uint32_t>(i), slot.generation}, *extra);
        }
    }
    if (result.status == StepStatus::awaiting_input && enemy_hp != 0) {
        if (enemy_hp < 1 || enemy_hp > 40) return {StepStatus::unsupported_content, rejection_boundary, Restriction::unsupported_setup};
        destination.enemy.hp = enemy_hp; destination.enemy.max_hp = enemy_hp;
    }
    return result;
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
} // namespace sts2::engine
