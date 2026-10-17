#pragma once
#include <array>
#include <cstdint>
#include <type_traits>

namespace sts2 {
// Scaffold rules only; these values are not validated against the game.
enum class Card : std::uint8_t { attack, defend };
enum class Outcome : std::uint8_t { ongoing, victory, defeat };
struct CombatState {
    int player_hp = 30;
    int player_block = 0;
    int enemy_hp = 24;
    int energy = 3;
    int turn = 1;
    std::array<Card, 5> hand{Card::attack, Card::defend, Card::attack,
                             Card::defend, Card::attack};
    std::uint8_t hand_size = 5;
    Outcome outcome = Outcome::ongoing;
};
static_assert(std::is_trivially_copyable_v<CombatState>);
} // namespace sts2
