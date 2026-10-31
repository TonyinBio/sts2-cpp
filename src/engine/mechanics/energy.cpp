#include "energy.hpp"

namespace sts2::engine::gameplay {
void spend_energy(CombatView state, int amount) noexcept { state.energy -= amount; }
void refresh_energy(CombatView state, int amount) noexcept { state.energy = amount; }
}
