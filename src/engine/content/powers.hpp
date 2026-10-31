#pragma once
#include "sts2/state/combat.hpp"

namespace sts2::engine::gameplay {
struct PowerModifier { int amount; std::uint32_t artifact; };
PowerModifier before_power_application(const CreatureState&, const OverflowStorage&, PowerId, int amount) noexcept;
void after_power_application(CombatView, Scratch&, CreatureState&, PowerModifier, bool enemy, bool enemy_side, CreatureHandle target);
// Combine the supported damage modifiers before one final rounding operation.
int modify_attack_damage(const CreatureState& target, int base, const CreatureState& source, const OverflowStorage&) noexcept;
std::int64_t modify_block(const CreatureState&, const OverflowStorage&, int amount) noexcept;
int preview_attack_damage(ConstCombatView, int value, bool active_preview) noexcept;
int preview_block(ConstCombatView, int value, bool active_preview) noexcept;
bool skip_initial_power_tick(PowerId, bool enemy_owner) noexcept;
bool power_is_depleted(PowerId, int amount) noexcept;
bool valid_power_amount(PowerId, int amount) noexcept;
bool power_ticks_after_enemy_turn(PowerId) noexcept;
}
