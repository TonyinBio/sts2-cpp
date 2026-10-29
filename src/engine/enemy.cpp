#include "gameplay.hpp"

namespace sts2::engine::gameplay {
void execute_enemy_move(CombatView state, Scratch& scratch) {
    switch (state.move) {
    case BeetleMove::shrink: (void)apply_power(state, scratch, PowerId::shrink, -1, false, true, {}, true, true); state.move = BeetleMove::chomp; break;
    case BeetleMove::chomp: case BeetleMove::stomp: {
        const auto hit = damage(state.player, state.move == BeetleMove::chomp ? 7 : 13, state.enemy);
        emit(state, scratch, CombatEventKind::damaged, true, false, {}, 0, 0, hit);
        emit(state, scratch, CombatEventKind::attacked, true, true, {}, 0, 0, hit);
        if (state.player.hp > 0) state.move = state.move == BeetleMove::chomp ? BeetleMove::stomp : BeetleMove::chomp;
        break;
    }
    }
}
EnemyIntent visible_enemy_intent(ConstCombatView state) noexcept {
    const auto move = state.move;
    const auto base = move == BeetleMove::chomp ? 7 : move == BeetleMove::stomp ? 13 : 0;
    return {move, base == 0 ? 0 : modify_attack_damage(state.player, base, state.enemy)};
}
}
