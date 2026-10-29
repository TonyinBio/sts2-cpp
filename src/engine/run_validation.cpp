#include "sts2/engine/run.hpp"
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
bool bag(ConstRelicBag value) noexcept {
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
    auto state_view = state.view();
    if (!valid_data_storage(state.data, state.overflow) ||
        (!state.data.combat && !state.combat_overflow.empty())) return false;
    const auto& setup = state_view;
    if (state_view.phase == RunPhase::uninitialized || state_view.phase >= RunPhase::invalid || state_view.act_index != 0 ||
        state_view.boundary.value == 0 || setup.seed.empty() || setup.seed.size() > 4096 || game_seed(setup.seed) != setup.numeric_seed ||
        !SetupRules::character(setup.character) || setup.ascension > 10 || setup.max_hp < 1 || setup.hp < 0 || setup.hp > setup.max_hp ||
        setup.gold < 0 || setup.orb_slots < 0 || setup.potion_slots < 0 || setup.potion_slots > 4096 || setup.deck.size() > 4096 ||
        setup.potions.size() > static_cast<std::size_t>(setup.potion_slots) || !std::isfinite(setup.card_rarity_offset) ||
        !std::isfinite(setup.potion_reward_odds) || !bag(setup.shared_relics) || !bag(setup.player_relics)) return false;
    if (state_view.clock.elapsed_seconds() != std::int64_t(state_view.boundary.value - 1) * state_view.clock.config().seconds_per_action ||
        !random(state_view.event_rng.state())) return false;
    for (const auto& rng : setup.rng.streams) if (!random(rng.state())) return false;
    for (const auto& rng : setup.player_rng.streams) if (!random(rng.state())) return false;
    if (!setup.deck.pool.valid()) return false;
    for (const auto h : setup.deck.handles())
        if (!setup.deck.get(h) || std::count(setup.deck.handles().begin(), setup.deck.handles().end(), h) != 1) return false;
    std::size_t live_cards = 0;
    for (const auto& slot : setup.deck.pool.slots()) live_cards += slot.live;
    if (live_cards != setup.deck.size()) return false;
    for (std::size_t i = 0; i < setup.deck.size(); ++i) {
        if (!card(setup.deck[i])) return false;
        for (std::size_t j = 0; j < i; ++j) if (setup.deck[i].instance == setup.deck[j].instance) return false;
    }
    for (auto relic : setup.relics) if (!id(relic, "RELIC.")) return false;
    const auto joss = SetupRules::find("RELIC.JOSS_PAPER");
    if (setup.joss_paper_counter >= 5 ||
        (setup.joss_paper_counter != 0 && std::find(setup.relics.begin(), setup.relics.end(), joss) == setup.relics.end()) ||
        (state_view.combat && state_view.combat->joss_paper_counter != setup.joss_paper_counter)) return false;
    for (auto potion : setup.potions) if (potion != no_content && !id(potion, "POTION.")) return false;
    for (const auto& act : setup.acts) {
        if (!SetupRules::act(act.act) || !id(act.ancient, "EVENT.") || !id(act.boss, "ENCOUNTER.") ||
            (act.second_boss != no_content && !id(act.second_boss, "ENCOUNTER.")) || act.events_visited > act.events.size() + 1 ||
            act.normal_visited > act.normal.size() || act.elites_visited > act.elites.size() || act.bosses_visited > 2) return false;
        for (auto event : act.events) if (!id(event, "EVENT.")) return false;
        for (auto ancient : act.shared_ancients) if (!id(ancient, "EVENT.")) return false;
        for (auto encounter : act.normal) if (!id(encounter, "ENCOUNTER.")) return false;
        for (auto encounter : act.elites) if (!id(encounter, "ENCOUNTER.")) return false;
    }
    for (const auto& map : state_view.maps) if (map && (!valid_map(*map) || !random(map->generator_state))) return false;
    if (!state_view.maps[0] || state_view.current >= state_view.maps[0]->count || !state_view.maps[0]->nodes[state_view.current].active ||
        state_view.visited.empty() || state_view.visited.front() != state_view.maps[0]->start || state_view.visited.back() != state_view.current) return false;
    for (auto node : state_view.visited) if (node >= state_view.maps[0]->count || !state_view.maps[0]->nodes[node].active) return false;
    for (auto relic : state_view.neow_options) if (!id(relic, "RELIC.")) return false;
    if (state_view.selected_neow != no_content && std::find(state_view.neow_options.begin(), state_view.neow_options.end(), state_view.selected_neow) == state_view.neow_options.end()) return false;
    auto linked_cards = [](ConstCombatView combat, BasicDeckView<true> deck) noexcept {
        for (const auto& slot : combat.cards.slots()) {
            if (!slot.live || !slot.value.origin.present()) continue;
            const auto* found = deck.get(slot.value.origin);
            if (!found || SetupRules::name(found->definition) != Ruleset::pinned().card(slot.value.definition)->model_id) return false;
        }
        return true;
    };
    if (state_view.combat && state_view.combat_generation == 0) return false;
    if (state_view.combat) {
        const auto& combat = *state_view.combat;
        std::size_t listener = 0; bool burning_blood = false;
        for (auto relic : setup.relics) {
            const auto name = SetupRules::name(relic);
            burning_blood |= name == "RELIC.BURNING_BLOOD";
            if (name == "RELIC.CHARONS_ASHES" || name == "RELIC.JOSS_PAPER") {
                const auto expected = name == "RELIC.CHARONS_ASHES" ? ExhaustRelic::charons_ashes : ExhaustRelic::joss_paper;
                if (listener >= combat.exhaust_relic_count || combat.exhaust_relics[listener++] != expected) return false;
            }
        }
        if (listener != combat.exhaust_relic_count || combat.burning_blood != burning_blood) return false;
        for (const auto& slot : combat.cards.slots()) if (slot.generation < state_view.combat_generation) return false;
        if (!valid_combat(combat) || !linked_cards(combat, setup.deck) || combat.seed != setup.seed || combat.player.hp != setup.hp || combat.player.max_hp != setup.max_hp || combat.rng != setup.rng) return false;
        if (state_view.phase == RunPhase::combat) {
            if ((combat.phase != CombatPhase::play && combat.phase != CombatPhase::discard_choice && combat.phase != CombatPhase::card_choice) || combat.clock != state_view.clock || combat.boundary != state_view.boundary) return false;
        } else if (state_view.phase == RunPhase::complete) {
            if (!state_view.direct_combat || combat.phase != CombatPhase::player_died) return false;
        } else if (state_view.phase == RunPhase::combat_done) {
            if (!state_view.direct_combat || combat.phase != CombatPhase::combat_won) return false;
        } else return false;
        if (state_view.direct_combat && (state_view.rewards || state_view.phase == RunPhase::map)) return false;
    } else {
        if (state_view.phase == RunPhase::combat || state_view.direct_combat) return false;
        if ((state_view.phase == RunPhase::neow || state_view.phase == RunPhase::neow_done) && state_view.combat_generation != 0) return false;
        if ((state_view.phase == RunPhase::combat_done || state_view.phase == RunPhase::rewards || state_view.phase == RunPhase::complete) && state_view.combat_generation == 0) return false;
        if ((state_view.phase == RunPhase::complete) != (setup.hp == 0)) return false;
    }
    if (state_view.rewards) {
        const auto& rewards = *state_view.rewards;
        if (state_view.combat_generation == 0 || !rewards_valid(rewards) || (state_view.phase != RunPhase::rewards && state_view.phase != RunPhase::map)) return false;
    } else if (state_view.phase == RunPhase::rewards) return false;
    return true;
}
} // namespace sts2::engine
