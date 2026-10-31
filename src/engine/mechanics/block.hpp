#pragma once
#include "sts2/state/combat.hpp"

namespace sts2::engine::gameplay {
bool gain_block(CombatView, Scratch&, int amount, CardHandle source, std::uint32_t play);
}
