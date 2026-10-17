#include "sts2/simulator.hpp"
#include <algorithm>

namespace sts2 {
LegalActions legal_actions(const CombatState& state) noexcept {
    LegalActions result;
    if (state.outcome != Outcome::ongoing) return result;
    if (state.energy > 0) {
        for (std::uint8_t i = 0; i < state.hand_size; ++i)
            result.actions[result.size++] = {ActionKind::play_card, i};
    }
    result.actions[result.size++] = {ActionKind::end_turn};
    return result;
}

bool step(CombatState& state, Action action) noexcept {
    if (state.outcome != Outcome::ongoing) return false;
    if (action.kind == ActionKind::play_card) {
        if (state.energy <= 0 || action.card_index >= state.hand_size) return false;
        const auto card = state.hand[action.card_index];
        --state.energy;
        for (auto i = action.card_index; i + 1 < state.hand_size; ++i)
            state.hand[i] = state.hand[i + 1];
        --state.hand_size;
        if (card == Card::attack) {
            state.enemy_hp = std::max(0, state.enemy_hp - 6);
            if (state.enemy_hp == 0) state.outcome = Outcome::victory;
        } else {
            state.player_block += 5;
        }
        return true;
    }
    if (action.kind != ActionKind::end_turn) return false;
    state.player_hp = std::max(0, state.player_hp - std::max(0, 8 - state.player_block));
    state.player_block = 0;
    if (state.player_hp == 0) {
        state.outcome = Outcome::defeat;
        return true;
    }
    ++state.turn;
    state.energy = 3;
    // Fixed hand refill until draw/discard piles and shuffle replay are implemented.
    state.hand = {Card::attack, Card::defend, Card::attack, Card::defend, Card::attack};
    state.hand_size = 5;
    return true;
}
} // namespace sts2
