#pragma once
#include "sts2/core/storage.hpp"
#include "sts2/content/cards.hpp"
#include "sts2/content/catalog.hpp"
#include <variant>

namespace sts2::engine {
struct PersistentCard {
    ContentId definition = no_content;
    std::uint32_t instance = 0;
    std::uint32_t upgrade = 0;
    std::int32_t floor_added = 1;
    friend bool operator==(const PersistentCard&, const PersistentCard&) = default;
};
enum class Zone : std::uint8_t { hand, draw, discard, exhaust, play, count };
struct CombatCard {
    CardId definition = CardId::strike;
    bool upgraded = false;
    std::uint32_t creation_order = 0;
    PersistentCardHandle origin {};
    std::int32_t last_finished_player_turn = 0;
    bool removed = false;
    friend bool operator==(const CombatCard&, const CombatCard&) = default;
};
// Only cards with exceptional state occupy these side stores. Extend the
// alternatives with audited, card-owned payloads as content is implemented.
struct TheScytheExtraData {
    std::int32_t current_damage = 0;
    std::int32_t increased_damage = 0;
    friend bool operator==(const TheScytheExtraData&, const TheScytheExtraData&) = default;
};
using CardExtraData = std::variant<TheScytheExtraData>;
struct PersistentCardExtra {
    PersistentCardHandle card {};
    CardExtraData data {};
    friend bool operator==(const PersistentCardExtra&, const PersistentCardExtra&) = default;
};
struct CombatCardExtra {
    CardHandle card {};
    CardExtraData data {};
    friend bool operator==(const CombatCardExtra&, const CombatCardExtra&) = default;
};
static_assert(std::is_trivially_copyable_v<PersistentCardExtra>);
static_assert(std::is_trivially_copyable_v<CombatCardExtra>);
enum class CostModifierKind : std::uint8_t { absolute, relative };
enum class CostExpiry : std::uint8_t { combat = 0, turn = 2, played = 4, turn_or_played = 6 };
struct CostModifier {
    CardHandle card {};
    CostModifierKind kind = CostModifierKind::relative;
    CostExpiry expiry = CostExpiry::combat;
    std::int32_t amount = 0;
    bool reduce_only = false;
    friend bool operator==(const CostModifier&, const CostModifier&) = default;
};
using CardPool = PoolData<CombatCard, CombatCardTag, 32>;
}
