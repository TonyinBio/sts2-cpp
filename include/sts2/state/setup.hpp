#pragma once
#include "sts2/rules/setup.hpp"
#include "sts2/state/map.hpp"
#include "sts2/state/clock.hpp"
#include "sts2/core/result.hpp"
#include <string>
#include <vector>

namespace sts2::engine {
struct PlayerRngBank {
    std::array<GameRng, 3> streams;
    explicit PlayerRngBank(std::string_view seed = {}) noexcept;
    friend bool operator==(const PlayerRngBank&, const PlayerRngBank&) = default;
};
struct RelicBag {
    std::array<std::vector<ContentId>, 8> tiers;
    std::vector<RelicTier> order;
    friend bool operator==(const RelicBag&, const RelicBag&) = default;
};
struct ActRooms {
    ContentId act = no_content, ancient = no_content, boss = no_content, second_boss = no_content;
    std::vector<ContentId> events, normal, elites, shared_ancients;
    std::uint32_t events_visited = 0, normal_visited = 0, elites_visited = 0, bosses_visited = 0;
    friend bool operator==(const ActRooms&, const ActRooms&) = default;
};
struct PersistentCard {
    ContentId definition = no_content;
    std::uint32_t instance = 0;
    std::uint32_t upgrade = 0;
    std::int32_t floor_added = 1;
    friend bool operator==(const PersistentCard&, const PersistentCard&) = default;
};
struct SetupState {
    std::string seed;
    std::uint64_t numeric_seed = 0;
    ContentId character = no_content;
    std::uint8_t ascension = 0;
    std::int32_t hp = 0, max_hp = 0, gold = 0, potion_slots = 3, orb_slots = 0;
    RngBank rng;
    PlayerRngBank player_rng;
    RelicBag shared_relics, player_relics;
    std::array<ActRooms, 3> acts;
    std::vector<PersistentCard> deck;
    std::vector<ContentId> relics;
    std::vector<ContentId> potions; // Slot order; no_content denotes an empty slot.
    float card_rarity_offset = -0.05f, potion_reward_odds = 0.4f;
    friend bool operator==(const SetupState&, const SetupState&) = default;
};
struct SeededConfig {
    std::string_view build = "0.111.0+41cef1ea";
    std::string_view character = "CHARACTER.IRONCLAD";
    std::string_view seed = "ORACLECHECK01";
    std::uint8_t ascension = 0;
    std::array<std::string_view, 3> acts = {"ACT.OVERGROWTH", "ACT.HIVE", "ACT.GLORY"};
    ClockConfig clock;
};
// Builds persistent inventory, bags, all act encounter/event queues and RNG state
// from immutable definitions. Does not use a native save or expected snapshot.
[[nodiscard]] StepResult initialize_setup(SetupState&, const SeededConfig&);
} // namespace sts2::engine
