#include "sts2/simulator.hpp"
#include <iostream>

int main() {
    auto state = sts2::make_starter_combat();
    while (state.outcome == sts2::Outcome::ongoing) {
        const auto actions = sts2::legal_actions(state);
        if (!sts2::step(state, actions.actions[0])) return 1;
    }
    std::cout << "outcome=" << (state.outcome == sts2::Outcome::victory ? "victory" : "defeat")
              << " turns=" << state.turn << " hp=" << state.player_hp << '\n';
}
