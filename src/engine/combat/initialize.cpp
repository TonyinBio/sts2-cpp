#include "sts2/engine/combat.hpp"
#include "lifecycle.hpp"
#include "trace.hpp"
#include "../mechanics/cards.hpp"
#include <algorithm>
#include <new>
#include <stdexcept>

namespace sts2::engine {
using namespace gameplay;
namespace {
constexpr std::array starting_deck = {
    CardSpec{CardId::strike}, CardSpec{CardId::strike}, CardSpec{CardId::strike},
    CardSpec{CardId::strike}, CardSpec{CardId::strike}, CardSpec{CardId::defend},
    CardSpec{CardId::defend}, CardSpec{CardId::defend}, CardSpec{CardId::defend}, CardSpec{CardId::bash}};
}
StepResult initialize_combat(CombatView state, const CombatConfig& config, Scratch& scratch, const Ruleset& rules, Limits limits) {
    if (config.build != game_build || config.ascension != 0 || config.encounter != "ENCOUNTER.SHRINKER_BEETLE_WEAK" ||
        config.seed.empty() || config.seed.size() > 4096 || config.player_max_hp < 1 || config.player_hp < 1 || config.player_hp > config.player_max_hp ||
        config.card_generation == 0 || config.enemy_hp < 0 || config.enemy_hp > 40 || config.exhaust_relics.size() > 2 || config.joss_paper_counter >= 5 ||
        (config.seed.starts_with("old") && std::any_of(config.seed.begin(), config.seed.end(), [](unsigned char c) { return c >= 128; })))
        return {StepStatus::unsupported_content, state.boundary, Restriction::unsupported_setup};
    const auto deck = config.deck.empty() ? std::span<const CardSpec>(starting_deck) : config.deck;
    if (deck.size() > limits.cards) return {StepStatus::resource_limit, state.boundary, Restriction::capacity};
    for (const auto& card : deck) if (!rules.card(card.definition))
        return {StepStatus::unsupported_content, state.boundary, Restriction::unsupported_setup};
    for (std::size_t i = 0; i < config.exhaust_relics.size(); ++i)
        if (static_cast<unsigned>(config.exhaust_relics[i]) > 1 ||
            std::find(config.exhaust_relics.begin(), config.exhaust_relics.begin() + i, config.exhaust_relics[i]) != config.exhaust_relics.begin() + i)
            return {StepStatus::unsupported_content, state.boundary, Restriction::unsupported_setup};
    if (config.joss_paper_counter != 0 && std::find(config.exhaust_relics.begin(), config.exhaust_relics.end(), ExhaustRelic::joss_paper) == config.exhaust_relics.end())
        return {StepStatus::unsupported_content, state.boundary, Restriction::unsupported_setup};
    CombatState next(state.overflow.allocator()); auto next_view = next.view();
    if (!next_view.clock.initialize(config.clock)) return {StepStatus::unsupported_content, state.boundary, Restriction::unsupported_setup};
    try {
        next_view.exhaust_relic_count = static_cast<std::uint8_t>(config.exhaust_relics.size());
        std::copy(config.exhaust_relics.begin(), config.exhaust_relics.end(), next_view.exhaust_relics.begin());
        next_view.joss_paper_counter = config.joss_paper_counter;
        next_view.reserve(deck.size()); next_view.seed = config.seed; next_view.numeric_seed = game_seed(config.seed); next_view.rng = config.initial_rng ? *config.initial_rng : RngBank(config.seed);
        next_view.player = {config.player_hp, config.player_max_hp};
        int enemy_hp = config.enemy_hp;
        if (enemy_hp == 0) (void)next_view.rng.get(RunStream::niche).next_int(38, 41, enemy_hp);
        next_view.enemy = {enemy_hp, enemy_hp}; next_view.energy = 3; next_view.turn = 1; next_view.burning_blood = config.burning_blood;
        for (const auto& card : deck) {
            auto handle = next_view.cards.create({card.definition, card.upgraded, next_view.next_creation_order++, card.origin}, limits.cards, config.card_generation);
            if (!handle.present()) return {StepStatus::resource_limit, state.boundary, Restriction::capacity};
            next_view.zone(Zone::draw).push_back(handle);
        }
        next_view.rng.get(RunStream::shuffle).shuffle<CardHandle>(next_view.zone(Zone::draw));
        emit(next, scratch, CombatEventKind::combat_started);
        draw(next, scratch, 5, rules); next_view.phase = CombatPhase::play; next_view.boundary.value = 1;
        emit(next, scratch, CombatEventKind::combat_updated);
    } catch (const std::bad_alloc&) { return {StepStatus::resource_limit, state.boundary, Restriction::capacity}; } catch (const std::length_error&) { return {StepStatus::resource_limit, state.boundary, Restriction::capacity}; }
    state.replace(std::move(next)); scratch.steps = 0;
    return result(state);
}
}
