#pragma once
#include "sts2/rng.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace sts2 {
inline constexpr std::size_t max_cards = 10;
// Model order is ordinal BASH, DEFEND_IRONCLAD, STRIKE_IRONCLAD.
enum class Card : std::uint8_t { bash, defend, strike };
enum class Outcome : std::uint8_t { ongoing, victory, defeat };
enum class Move : std::uint8_t { shrink, chomp, stomp };
enum class RunRng : std::uint8_t {
    up_front, shuffle, unknown_map_point, combat_card_generation,
    combat_potion_generation, combat_card_selection, combat_energy_costs,
    combat_targets, monster_ai, niche, combat_orbs, treasure_room_relics,
};
inline constexpr std::size_t rng_count = 15; // 12 run + 3 player streams
using CardId = std::uint8_t;

struct Pile {
    std::array<CardId, max_cards> cards{};
    std::uint8_t size = 0;
    constexpr void push(CardId card) noexcept { cards[size++] = card; }
    constexpr CardId erase(std::uint8_t index) noexcept {
        const auto card = cards[index];
        for (auto i = index; i + 1 < size; ++i) cards[i] = cards[i + 1];
        cards[--size] = 0;
        return card;
    }
    constexpr void clear() noexcept { cards = {}; size = 0; }
    bool operator==(const Pile&) const = default;
};

struct Power {
    std::int32_t amount = 0; // zero = absent; Shrink is infinite (-1)
    std::int32_t amount_on_turn_start = 0;
    std::uint32_t instance = 0;
    bool skip_next_duration_tick = false;
    bool operator==(const Power&) const = default;
};

// Supported profile: Ironclad A0 starter deck vs one Shrinker Beetle.
// Copy by value to branch search. No owning pointers, heap storage or JSON.
struct CombatState {
    Rng shuffle_rng; // only stream consumed by this combat profile
    std::array<Card, max_cards> cards{}; // stable numeric instance identity
    Pile hand, draw, discard;
    Power shrink, vulnerable;
    std::int32_t player_hp = 80, player_max_hp = 80, player_block = 0;
    std::int32_t enemy_hp = 38, enemy_max_hp = 38, enemy_block = 0;
    std::uint32_t turn = 1;
    std::uint32_t moves_performed = 0; // fixed AI history can be reconstructed
    std::uint32_t next_power_instance = 0;
    std::int32_t energy = 3, max_energy = 3;
    Outcome outcome = Outcome::ongoing;
    Move enemy_move = Move::shrink;
    bool current_move_performed = false;
    std::uint8_t card_count = 10;
    bool operator==(const CombatState&) const = default;
};
static_assert(std::is_trivially_copyable_v<CombatState>);
static_assert(sizeof(CombatState) <= 256, "Keep branch copies small");
} // namespace sts2
