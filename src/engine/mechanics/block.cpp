#include "block.hpp"
#include "../content/powers.hpp"
#include "../combat/trace.hpp"
#include <algorithm>
#include <limits>

namespace sts2::engine::gameplay {
bool gain_block(CombatView state, Scratch& scratch, int amount, CardHandle source, std::uint32_t play) {
    const auto value = modify_block(state.player, state.creature_overflow, amount);
    if (value > std::numeric_limits<int>::max() - state.player.block) return false;
    state.player.block += static_cast<int>(value);
    emit(state, scratch, CombatEventKind::block_gained, false, false, source, play, static_cast<int>(value));
    return true;
}
}
