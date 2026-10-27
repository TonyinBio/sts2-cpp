#include "sts2/engine/combat.hpp"
#include "sts2/observation/observe.hpp"
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <new>

namespace {
bool count_allocations = false;
std::uint64_t allocations = 0;
}
void* operator new(std::size_t bytes) {
    if (void* p = std::malloc(bytes == 0 ? 1 : bytes)) { if (count_allocations) ++allocations; return p; }
    throw std::bad_alloc();
}
void* operator new[](std::size_t bytes) { return ::operator new(bytes); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

int main(int argc, char** argv) {
    using namespace sts2::engine;
    std::uint64_t battles = 100000;
    if (argc == 2) {
        char* end = nullptr; battles = std::strtoull(argv[1], &end, 10);
        if (!end || *end != '\0' || battles == 0 || battles > 100000000) return 1;
    } else if (argc > 2) return 1;
    CombatState baseline, state; Scratch scratch;
    if (initialize_combat(baseline, {}, scratch).status != StepStatus::awaiting_input) return 1;
    if (clone_into(state, baseline) != CloneStatus::cloned) throw std::bad_alloc{};
    std::uint64_t transitions = 0, checksum = 0;
    const auto start = std::chrono::steady_clock::now();
    count_allocations = true;
    for (std::uint64_t battle = 0; battle < battles; ++battle) {
        if (clone_into(state, baseline) != CloneStatus::cloned) throw std::bad_alloc{};
        while (state.view().phase == CombatPhase::play) {
            CombatInput input{state.view().boundary, CombatInputKind::end_turn};
            ActionSpaceView actions(state);
            for (std::size_t i = 0; i < state.zone(Zone::hand).size(); ++i) {
                const auto action = *actions.at(i);
                if (action.restriction == Restriction::none && action.input.target_enemy) { input = action.input; break; }
            }
            if (input.kind == CombatInputKind::end_turn) for (std::size_t i = 0; i < actions.size(); ++i) {
                const auto action = *actions.at(i);
                if (action.restriction == Restriction::none) { input = action.input; break; }
            }
            const auto result = apply_combat(state, input, scratch);
            ++transitions;
            if (result.status == StepStatus::engine_error || result.status == StepStatus::invalid_action || result.status == StepStatus::resource_limit) return 2;
        }
        checksum += std::uint64_t(state.view().player.hp) + state.view().history.size() + state.view().plays.size();
    }
    count_allocations = false;
    const auto seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::cout << "Isolated A0 Shrinker combat; no observation text or serialization; includes clone reset\n"
              << "battles=" << battles << " transitions=" << transitions << " seconds=" << seconds
              << " transitions_per_second=" << static_cast<double>(transitions) / seconds
              << " battles_per_second=" << static_cast<double>(battles) / seconds << '\n'
              << "state_root_bytes=" << sizeof(CombatState) << " retained_state_bytes=" << state.retained_bytes()
              << " warmed_allocations=" << allocations << " checksum=" << checksum << '\n';
    return allocations == 0 ? 0 : 3;
}
