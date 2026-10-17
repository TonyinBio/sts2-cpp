#include "sts2/simulator.hpp"
#include <chrono>
#include <cstdint>
#include <iostream>

int main() {
    constexpr int simulations = 1000000;
    std::uint64_t checksum = 0;
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < simulations; ++i) {
        sts2::CombatState state;
        state.enemy_hp += i % 7;
        while (state.outcome == sts2::Outcome::ongoing) {
            const auto actions = sts2::legal_actions(state);
            if (!sts2::step(state, actions.actions[0])) return 1;
        }
        checksum += static_cast<std::uint64_t>(state.player_hp + state.turn);
    }
    const double seconds = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - start).count();
    std::cout << simulations / seconds << " simulations/s; checksum=" << checksum << '\n';
}
