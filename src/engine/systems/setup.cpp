#include "sts2/engine/run.hpp"
#include "sts2/rules/ruleset.hpp"
#include <algorithm>
#include <new>
#include <stdexcept>

namespace sts2::engine {
PlayerRngBank::PlayerRngBank(std::string_view seed) noexcept {
    constexpr std::array<std::string_view, 3> names{"rewards", "shops", "transformations"};
    for (std::size_t i = 0; i < streams.size(); ++i) streams[i] = GameRng(game_hash(seed) + game_hash(names[i]));
}
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
} // namespace sts2::engine
