#pragma once
#include "sts2/state.hpp"
#include <array>
#include <cstddef>
#include <cstdint>

namespace sts2 {
enum class ActionKind : std::uint8_t { play_card, end_turn };
// Card index is the current hand position, not a persistent card identity.
// Target 0 is the sole enemy; -1 means no target (Defend/end turn).
struct Action {
    ActionKind kind;
    std::uint8_t card_index = 0;
    std::int8_t target = -1;
    bool operator==(const Action&) const = default;
};
struct LegalActions {
    std::array<Action, max_cards + 1> actions{};
    std::size_t size = 0;
};
} // namespace sts2
