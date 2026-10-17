#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace sts2 {
enum class ActionKind : std::uint8_t { play_card, end_turn };
struct Action {
    ActionKind kind;
    std::uint8_t card_index = 0;
};
struct LegalActions {
    std::array<Action, 6> actions{};
    std::size_t size = 0;
};
} // namespace sts2
