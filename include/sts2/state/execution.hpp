#pragma once
#include "sts2/state/cards.hpp"
#include <array>
#include <type_traits>
#include <variant>

namespace sts2::engine {
// Every suspended operand is owned here, never on the C++ call stack or in Scratch.
enum class SelectionEffect : std::uint8_t { none, exhaust, upgrade, topdeck };
enum class ExhaustRelic : std::uint8_t { charons_ashes, joss_paper };
struct DrawContinuation {
    std::int32_t remaining = 0;
    bool from_hand_draw = true;
    friend bool operator==(const DrawContinuation&, const DrawContinuation&) = default;
};
struct ShuffleContinuation {
    std::uint32_t begin = 0, count = 0, cursor = 0;
    bool waiting = false;
    friend bool operator==(const ShuffleContinuation&, const ShuffleContinuation&) = default;
};
struct SelectionContinuation {
    SelectionEffect effect = SelectionEffect::none;
    Zone zone = Zone::hand;
    std::uint32_t begin = 0, count = 0, cursor = 0;
    friend bool operator==(const SelectionContinuation&, const SelectionContinuation&) = default;
};
struct ExhaustContinuation {
    std::array<ExhaustRelic, 2> candidates {};
    std::uint8_t count = 0, cursor = 0;
    friend bool operator==(const ExhaustContinuation&, const ExhaustContinuation&) = default;
};
struct RelicDrawCompletion { friend bool operator==(const RelicDrawCompletion&, const RelicDrawCompletion&) = default; };
using ContinuationPayload = std::variant<std::monostate, DrawContinuation, ShuffleContinuation, SelectionContinuation, ExhaustContinuation, RelicDrawCompletion>;
struct EffectFrame {
    CardHandle source;
    std::uint32_t instruction = 0;
    std::uint32_t play = 0;
    std::int32_t energy = 0;
    // Captured enemy operand, also for untargeted programs such as Shockwave.
    CreatureHandle target {};
    ContinuationPayload pending {};
    friend bool operator==(const EffectFrame&, const EffectFrame&) = default;
};
static_assert(std::is_trivially_copyable_v<EffectFrame>);
struct CardSelection {
    PromptId id {};
    SelectionEffect effect = SelectionEffect::none;
    Zone zone = Zone::hand;
    std::uint32_t minimum = 0, maximum = 0;
    bool automatic_at_maximum = false;
    std::uint32_t continuation_depth = 0;
    friend bool operator==(const CardSelection&, const CardSelection&) = default;
};
// These compiled reactions are used by controlled architecture fixtures only;
// their presence does not establish native content support.
enum class ShuffleReactionKind : std::uint8_t { choose_card, nested_draw };
struct ShuffleReaction {
    ShuffleReactionKind kind = ShuffleReactionKind::choose_card;
    bool fired = false;
    friend bool operator==(const ShuffleReaction&, const ShuffleReaction&) = default;
};
using ShuffleReactionPool = PoolData<ShuffleReaction, ShuffleReactionTag, 4>;
}
