#pragma once
#include "sts2/engine/combat.hpp"

// Internal procedural gameplay interfaces; all sources compile into sts2_engine.
namespace sts2::engine::gameplay {
StepResult result(ConstCombatView, Restriction = Restriction::none) noexcept;
StepResult fail(CombatView, Restriction) noexcept;
bool consume(Scratch&) noexcept;
void emit(CombatView, Scratch&, CombatEventKind, bool enemy_side = false, bool actor_enemy = false,
    CardHandle = {}, std::uint32_t object = 0, int amount = 0, HitResult = {}, bool has_applier = true, bool applier_enemy = false);
std::uint32_t apply_power(CombatView, Scratch&, PowerId, int amount, bool enemy, bool enemy_side,
    CardHandle source = {}, bool has_applier = true, bool applier_enemy = false);
void expire_power(CombatView, Scratch&, CreatureState&, PowerId);
void snapshot_power_turn(CombatView, Scratch&, CreatureState&, bool enemy);
void move_card(CombatView, Zone from, Zone to, CardHandle);
void clear_selection(CombatView);
void begin_selection(CombatView, SelectionEffect, Zone, std::uint32_t minimum, std::uint32_t maximum, bool automatic);
void reshuffle(CombatView, const Ruleset&);
void draw_one(CombatView, Scratch&, bool from_hand_draw);
bool valid_continuations(ConstCombatView, const Ruleset&) noexcept;
StepResult resume_continuation(CombatView, Scratch&, const Ruleset&);
void draw(CombatView, Scratch&, int count, const Ruleset&, bool from_hand_draw = true);
void exhaust(CombatView, Scratch&, Zone, CardHandle);
bool gain_block(CombatView, Scratch&, int amount, CardHandle source, std::uint32_t play);
int heal(CreatureState&, int amount) noexcept;
void spend_energy(CombatView, int amount) noexcept;
void refresh_energy(CombatView, int amount) noexcept;
HitResult receive_damage(CreatureState& target, int amount) noexcept;
HitResult damage(CreatureState& target, int base, const CreatureState& source) noexcept;
// Typed hooks distinguish synchronous value modification from ordered reactions.
struct PowerModifier { int amount; std::uint32_t artifact; };
PowerModifier before_power_application(const CreatureState&, PowerId, int amount) noexcept;
void after_power_application(CombatView, Scratch&, CreatureState&, PowerModifier, bool enemy, bool enemy_side);
std::int64_t modify_block(const CreatureState&, int amount) noexcept;
int modify_attack_damage(const CreatureState& target, int base, const CreatureState& source) noexcept;
void react_to_exhaust(ExhaustRelic, CombatView, Scratch&);
void after_enemy_turn(CombatView, Scratch&);
void after_card_exhausted(CombatView, Scratch&, CardHandle);
StepResult execute_instruction(CombatView, Scratch&, const Ruleset&);
struct EnemyIntent { BeetleMove move; int attack; };
EnemyIntent visible_enemy_intent(ConstCombatView) noexcept;
void execute_enemy_move(CombatView, Scratch&);
void end_combat(CombatView, Scratch&, bool won);
}
