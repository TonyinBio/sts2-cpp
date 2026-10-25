#pragma once
#include "sts2/core/ids.hpp"
#include <cstdint>

namespace sts2::engine {
enum class HistoryKind : std::uint8_t { drawn, discarded, energy_spent, play_started, play_finished, damaged, attacked, block_gained, power_received, exhausted };
struct HitResult {
    std::int32_t blocked = 0, unblocked = 0, overkill = 0;
    bool block_broken = false, fully_blocked = false, killed = false;
    friend bool operator==(const HitResult&, const HitResult&) = default;
};
struct CombatHistoryEntry {
    HistoryKind kind = HistoryKind::drawn;
    std::int32_t turn = 1;
    bool enemy_side = false, actor_enemy = false;
    CardHandle card;
    std::uint32_t object = 0; // Play or power index, interpreted by kind.
    std::int32_t amount = 0;
    HitResult hit;
    bool has_applier = true, applier_enemy = false;
    friend bool operator==(const CombatHistoryEntry&, const CombatHistoryEntry&) = default;
};
struct CardPlayRecord {
    CardHandle card;
    std::int32_t energy = 0;
    bool enemy_target = false;
    friend bool operator==(const CardPlayRecord&, const CardPlayRecord&) = default;
};
enum class PowerId : std::uint8_t { shrink, vulnerable, strength, dexterity, weak };
struct PowerRecord {
    PowerId definition = PowerId::shrink;
    std::int32_t amount = 0, amount_on_turn_start = 0;
    bool enemy_owner = false, active = true, skip_next_tick = false;
    bool applier_enemy = false, has_applier = true;
    CardHandle source;
    friend bool operator==(const PowerRecord&, const PowerRecord&) = default;
};
} // namespace sts2::engine
