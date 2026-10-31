#pragma once
#include "sts2/core/ids.hpp"
#include "sts2/content/powers.hpp"
#include <cstdint>

namespace sts2::engine {
enum class CombatEventKind : std::uint8_t { drawn, discarded, energy_spent, play_started, play_finished, damaged, attacked, block_gained, power_received, combat_started, combat_won, combat_lost, power_changed, combat_updated, exhausted };
struct HitResult {
    std::int32_t blocked = 0, unblocked = 0, overkill = 0;
    bool block_broken : 1 = false;
    bool fully_blocked : 1 = false;
    bool killed : 1 = false;
    friend bool operator==(const HitResult&, const HitResult&) = default;
};
struct CombatEvent {
    CombatEventKind kind = CombatEventKind::drawn;
    std::int32_t turn = 1;
    bool enemy_side : 1 = false;
    bool actor_enemy : 1 = false;
    CardHandle card {};
    std::uint32_t object = 0; // Play or power identity, interpreted by kind.
    std::int32_t amount = 0;
    HitResult hit {};
    bool from_hand_draw : 1 = true;
    bool has_applier : 1 = true;
    bool applier_enemy : 1 = false;
    friend bool operator==(const CombatEvent&, const CombatEvent&) = default;
};
struct PowerInstance {
    std::uint32_t instance = 0;
    PowerId definition = PowerId::shrink;
    std::int32_t amount = 0, amount_on_turn_start = 0;
    bool enemy_owner : 1 = false;
    bool active : 1 = false;
    bool skip_next_tick : 1 = false;
    bool has_applier : 1 = true;
    bool applier_enemy : 1 = false;
    CardHandle source {};
    CreatureHandle applier {};
    enum class Stacking : std::uint8_t { by_type, by_applier, separate };
    Stacking stacking = Stacking::by_type;
    friend bool operator==(const PowerInstance&, const PowerInstance&) = default;
};
} // namespace sts2::engine
