#include "sts2/simulator.hpp"
#include <array>
#include <chrono>
#include <cstdint>
#include <iostream>

int main() {
    constexpr int simulations = 1000000;
    // Real variation in hand/shuffle state; setup and JSON are outside rollout timing.
    std::array<sts2::CombatState, 256> starts;
    for (std::size_t i = 0; i < starts.size(); ++i) starts[i] = sts2::make_starter_combat(i);
    std::uint64_t checksum = 0, transitions = 0;
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < simulations; ++i) {
        auto state = starts[static_cast<std::size_t>(i) % starts.size()];
        while (state.outcome == sts2::Outcome::ongoing) {
            const auto actions = sts2::legal_actions(state);
            if (!sts2::step(state, actions.actions[0])) return 1;
            ++transitions;
        }
        checksum += static_cast<std::uint64_t>(state.player_hp + state.turn);
    }
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::cout << "state_bytes=" << sizeof(sts2::CombatState)
              << " simulations/s=" << simulations / seconds
              << " transitions/s=" << transitions / seconds
              << " checksum=" << checksum << '\n';
}
