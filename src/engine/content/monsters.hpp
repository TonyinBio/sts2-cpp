#pragma once
#include "sts2/state/combat.hpp"

namespace sts2::engine::gameplay {
struct EnemyIntent { BeetleMove move; int attack; };
EnemyIntent visible_enemy_intent(ConstCombatView) noexcept;
void execute_enemy_move(CombatView, Scratch&);
}
