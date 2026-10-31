#pragma once
#include "sts2/state/combat.hpp"

namespace sts2::engine::gameplay {
void spend_energy(CombatView, int amount) noexcept;
void refresh_energy(CombatView, int amount) noexcept;
}
