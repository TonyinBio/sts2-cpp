#include "sts2/kernel/run_simulator.hpp"
#include <algorithm>
#include <cmath>

namespace sts2::engine {
namespace {
bool id(ContentId value, std::string_view prefix) noexcept { return SetupRules::name(value).starts_with(prefix); }
bool random(const RngState& state) noexcept { return (state.words[0] | state.words[1] | state.words[2] | state.words[3]) != 0; }
bool card(const PersistentCard& value) noexcept { return id(value.definition, "CARD.") && value.upgrade <= 1 && value.floor_added >= -1; }
bool rewards_valid(const CombatRewards& rewards) noexcept {
    if (rewards.gold < 0 || (rewards.potion != no_content && !id(rewards.potion, "POTION.")) ||
        (rewards.potion_taken && rewards.potion == no_content) || rewards.card_taken != (rewards.selected_card < rewards.cards.size()) ||
        (rewards.selected_card != UINT8_MAX && rewards.selected_card >= rewards.cards.size()) ||
        (rewards.selecting_card && (rewards.resolved || rewards.card_taken))) return false;
    for (const auto& offered : rewards.cards) if (!card(offered) || offered.floor_added != -1) return false;
    const bool all_taken = rewards.gold_taken && (rewards.potion == no_content || rewards.potion_taken) && rewards.card_taken;
    return rewards.resolved == (rewards.skipped || all_taken);
}
bool bag(const RelicBag& value) noexcept {
    if (value.order.size() > value.tiers.size()) return false;
    for (std::size_t i = 0; i < value.order.size(); ++i) {
        if (static_cast<std::size_t>(value.order[i]) >= value.tiers.size()) return false;
        for (std::size_t j = 0; j < i; ++j) if (value.order[i] == value.order[j]) return false;
    }
    for (const auto& tier : value.tiers) for (auto relic : tier) if (!id(relic, "RELIC.")) return false;
    return true;
}
}
bool valid_run(const RunState& state) noexcept {
    const auto& setup = state.setup;
    if (state.phase == RunPhase::uninitialized || state.phase >= RunPhase::invalid || state.act_index != 0 ||
        state.boundary.value == 0 || setup.seed.empty() || setup.seed.size() > 4096 || game_seed(setup.seed) != setup.numeric_seed ||
        !SetupRules::character(setup.character) || setup.ascension > 10 || setup.max_hp < 1 || setup.hp < 0 || setup.hp > setup.max_hp ||
        setup.gold < 0 || setup.orb_slots < 0 || setup.potion_slots < 0 || setup.potion_slots > 4096 || setup.deck.size() > 4096 ||
        setup.potions.size() > static_cast<std::size_t>(setup.potion_slots) || !std::isfinite(setup.card_rarity_offset) ||
        !std::isfinite(setup.potion_reward_odds) || !bag(setup.shared_relics) || !bag(setup.player_relics)) return false;
    if (state.clock.elapsed_seconds() != std::int64_t(state.boundary.value - 1) * state.clock.config().seconds_per_action ||
        state.initial_heal < 0 || state.gold_gained < 0 || !random(state.event_rng.state())) return false;
    for (const auto& rng : setup.rng.streams) if (!random(rng.state())) return false;
    for (const auto& rng : setup.player_rng.streams) if (!random(rng.state())) return false;
    for (std::size_t i = 0; i < setup.deck.size(); ++i) {
        if (!card(setup.deck[i])) return false;
        for (std::size_t j = 0; j < i; ++j) if (setup.deck[i].instance == setup.deck[j].instance) return false;
    }
    for (auto relic : setup.relics) if (!id(relic, "RELIC.")) return false;
    for (auto potion : setup.potions) if (potion != no_content && !id(potion, "POTION.")) return false;
    for (auto potion : state.discarded_potions) if (!id(potion, "POTION.")) return false;
    for (const auto& act : setup.acts) {
        if (!SetupRules::act(act.act) || !id(act.ancient, "EVENT.") || !id(act.boss, "ENCOUNTER.") ||
            (act.second_boss != no_content && !id(act.second_boss, "ENCOUNTER.")) || act.events_visited > act.events.size() + 1 ||
            act.normal_visited > act.normal.size() || act.elites_visited > act.elites.size() || act.bosses_visited > 2) return false;
        for (auto event : act.events) if (!id(event, "EVENT.")) return false;
        for (auto ancient : act.shared_ancients) if (!id(ancient, "EVENT.")) return false;
        for (auto encounter : act.normal) if (!id(encounter, "ENCOUNTER.")) return false;
        for (auto encounter : act.elites) if (!id(encounter, "ENCOUNTER.")) return false;
    }
    for (const auto& map : state.maps) if (map && (!valid_map(*map) || !random(map->generator_state))) return false;
    if (!state.maps[0] || state.current >= state.maps[0]->count || !state.maps[0]->nodes[state.current].active ||
        state.visited.empty() || state.visited.front() != state.maps[0]->start || state.visited.back() != state.current) return false;
    for (auto node : state.visited) if (node >= state.maps[0]->count || !state.maps[0]->nodes[node].active) return false;
    for (auto relic : state.neow_options) if (!id(relic, "RELIC.")) return false;
    if (state.selected_neow != no_content && std::find(state.neow_options.begin(), state.neow_options.end(), state.selected_neow) == state.neow_options.end()) return false;
    auto linked_cards = [](const State& combat, const std::vector<PersistentCard>& deck) noexcept {
        for (const auto& slot : combat.cards.slots()) {
            if (!slot.live || slot.value.deck_instance == UINT32_MAX) continue;
            const auto found = std::find_if(deck.begin(), deck.end(), [&](const auto& c) { return c.instance == slot.value.deck_instance; });
            if (found == deck.end() || SetupRules::name(found->definition) != Ruleset::pinned().card(slot.value.definition)->model_id) return false;
        }
        return true;
    };
    if (state.encounters.size() > max_retained_encounters || (!state.encounters.empty() && !state.combat)) return false;
    for (std::size_t i = 0; i < state.encounters.size(); ++i) {
        const auto& entry = state.encounters[i];
        if (!valid_state(entry.combat) || entry.combat.phase != Phase::combat_won || entry.combat.seed != setup.seed ||
            entry.node >= state.maps[0]->count || entry.floor == 0 || entry.floor > state.visited.size() ||
            state.visited[entry.floor - 1] != entry.node || !linked_cards(entry.combat, entry.deck) ||
            (i && entry.floor < state.encounters[i - 1].floor)) return false;
        for (std::size_t j = 0; j < entry.deck.size(); ++j) {
            if (!card(entry.deck[j])) return false;
            for (std::size_t k = 0; k < j; ++k) if (entry.deck[j].instance == entry.deck[k].instance) return false;
        }
        for (auto potion : entry.discarded_potions) if (!id(potion, "POTION.")) return false;
        if (entry.rewards && (!rewards_valid(*entry.rewards) || !entry.rewards->resolved || entry.rewards->selecting_card)) return false;
        if (entry.combat.boundary.value > state.boundary.value || entry.combat.clock.config() != state.clock.config() ||
            entry.combat.clock.elapsed_seconds() > state.clock.elapsed_seconds() ||
            (i && entry.combat.boundary.value < state.encounters[i - 1].combat.boundary.value)) return false;
        // This slice retains complete pools and never recycles a combat slot.
        // Each published encounter therefore owns one distinct generation.
        for (const auto& slot : entry.combat.cards.slots()) if (slot.generation != i + 1) return false;
    }
    if (state.combat) {
        const auto& combat = *state.combat;
        for (const auto& slot : combat.cards.slots()) if (slot.generation != state.encounters.size() + 1) return false;
        if (!valid_state(combat) || !linked_cards(combat, setup.deck) || combat.seed != setup.seed || combat.player.hp != setup.hp || combat.player.max_hp != setup.max_hp || combat.rng != setup.rng) return false;
        if (state.phase == RunPhase::combat) {
            if ((combat.phase != Phase::play && combat.phase != Phase::discard_choice) || combat.clock != state.clock || combat.boundary != state.boundary) return false;
        } else if (state.phase == RunPhase::complete) {
            if (combat.phase != Phase::player_died) return false;
        } else if (state.phase == RunPhase::combat_done || state.phase == RunPhase::rewards || state.phase == RunPhase::map) {
            if (combat.phase != Phase::combat_won) return false;
        } else return false;
        if (state.direct_combat && (state.rewards || state.phase == RunPhase::map)) return false;
    } else if (state.phase == RunPhase::combat || state.phase == RunPhase::combat_done || state.phase == RunPhase::rewards || state.phase == RunPhase::complete || state.direct_combat) return false;
    if (state.rewards) {
        const auto& rewards = *state.rewards;
        if (!state.combat || !rewards_valid(rewards) || (state.phase != RunPhase::rewards && state.phase != RunPhase::map)) return false;
    } else if (state.phase == RunPhase::rewards) return false;
    return true;
}
} // namespace sts2::engine
