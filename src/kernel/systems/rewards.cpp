#include "sts2/kernel/rewards.hpp"
#include <algorithm>
#include <new>
#include <stdexcept>

namespace sts2::engine {
namespace {
float uniform(GameRng& rng) { float value; if (!rng.next_float(0, 1, value)) throw std::logic_error("Invalid reward random bound"); return value; }
std::size_t index(GameRng& rng, std::size_t count) {
    int value;
    if (count > INT32_MAX || !rng.next_int(0, static_cast<int>(count), value)) throw std::logic_error("Empty reward pool");
    return static_cast<std::size_t>(value);
}
}
StepResult generate_combat_rewards(SetupState& setup, CombatRewards& destination, std::uint8_t act_index) {
    if (setup.ascension != 0 || act_index != 0) return {StepStatus::unsupported_content, {}, Restriction::unsupported_setup};
    const auto* character = SetupRules::character(setup.character);
    if (!character) return {StepStatus::unsupported_content, {}, Restriction::unsupported_setup};
    for (auto id : setup.relics) if (SetupRules::name(id) != "RELIC.BURNING_BLOOD" && SetupRules::name(id) != "RELIC.GOLDEN_PEARL")
        return {StepStatus::unsupported_content, {}, Restriction::unsupported_setup};
    try {
        auto rng = setup.player_rng.streams[0]; CombatRewards rewards;
        auto rarity_offset = setup.card_rarity_offset, potion_odds = setup.potion_reward_odds;
        const auto has_potion = uniform(rng) < potion_odds;
        potion_odds += has_potion ? -0.1f : 0.1f;
        rewards.gold = 10 + static_cast<int>(index(rng, 11));
        if (has_potion) {
            const auto roll = uniform(rng);
            const auto rarity = roll <= .1f ? ContentRarity::rare : roll <= .35f ? ContentRarity::uncommon : ContentRarity::common;
            std::vector<ContentId> options;
            for (const auto& p : character->potion_pool) if (p.rarity == rarity) options.push_back(p.id);
            for (const auto& p : SetupRules::shared_potions()) if (p.rarity == rarity) options.push_back(p.id);
            rewards.potion = options.at(index(rng, options.size()));
        }
        for (std::size_t i = 0; i < rewards.cards.size(); ++i) {
            const auto roll = uniform(rng), rare = .03f + rarity_offset;
            auto rarity = roll < rare ? ContentRarity::rare : roll < .37f + rare ? ContentRarity::uncommon : ContentRarity::common;
            rarity_offset = rarity == ContentRarity::rare ? -.05f : std::min(rarity_offset + .01f, .4f);
            std::vector<const CardEntry*> options;
            for (unsigned attempt = 0; attempt < 3; ++attempt) {
                for (const auto& c : character->card_pool) {
                    bool duplicate = false;
                    for (std::size_t previous = 0; previous < i; ++previous) if (rewards.cards[previous].definition == c.id) duplicate = true;
                    if (c.rarity == rarity && !duplicate) options.push_back(&c);
                }
                if (!options.empty()) break;
                rarity = static_cast<ContentRarity>((static_cast<unsigned>(rarity) + 1) % 3);
            }
            const auto& card = *options.at(index(rng, options.size()));
            const auto upgrade_roll = uniform(rng); // Native consumes this even for non-upgradable cards.
            rewards.cards[i] = {card.id, 0, card.upgradable && upgrade_roll <= 0 ? 1u : 0u, -1};
        }
        setup.player_rng.streams[0] = rng; setup.card_rarity_offset = rarity_offset; setup.potion_reward_odds = potion_odds;
        destination = rewards;
        return {StepStatus::awaiting_input, {}};
    } catch (const std::bad_alloc&) { return {StepStatus::resource_limit, {}, Restriction::capacity}; }
    catch (...) { return {StepStatus::engine_error, {}, Restriction::invalid_state}; }
}
} // namespace sts2::engine
