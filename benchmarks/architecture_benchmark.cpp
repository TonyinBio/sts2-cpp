#include "sts2/engine/action.hpp"
#include "../tests/support/continuation_fixture.hpp"
#include "../src/engine/mechanics/powers.hpp"
#include <stdexcept>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <new>

namespace { bool counting = false; std::uint64_t allocations = 0, checksum = 0; }
void* operator new(std::size_t n) {
    if (auto* p = std::malloc(n ? n : 1)) { if (counting) ++allocations; return p; }
    throw std::bad_alloc{};
}
void* operator new[](std::size_t n) { return ::operator new(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
void* operator new(std::size_t n, std::align_val_t alignment) {
    void* p = nullptr;
    if (posix_memalign(&p, static_cast<std::size_t>(alignment), n ? n : 1)) throw std::bad_alloc{};
    if (counting) ++allocations;
    return p;
}
void* operator new[](std::size_t n, std::align_val_t a) { return ::operator new(n, a); }
void operator delete(void* p, std::align_val_t) noexcept { std::free(p); }
void operator delete[](void* p, std::align_val_t) noexcept { std::free(p); }
void operator delete(void* p, std::size_t, std::align_val_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t, std::align_val_t) noexcept { std::free(p); }

using namespace sts2::engine;
void require(bool ok) { if (!ok) throw std::runtime_error("benchmark scenario failed"); }
void nested(RunState& state) {
    Scratch scratch;
    const std::array deck{CardSpec{CardId::burning_pact}, CardSpec{CardId::defend}, CardSpec{CardId::strike}, CardSpec{CardId::strike}, CardSpec{CardId::defend}};
    require(initialize_combat_scenario(state, {}, deck).status == StepStatus::awaiting_input);
    std::array<scenario::ShuffleReactionKind, 7> hooks;
    hooks.fill(scenario::ShuffleReactionKind::nested_draw); hooks.back() = scenario::ShuffleReactionKind::choose_card;
    scenario::configure_shuffle_reactions(state, hooks);
    for (auto definition : {CardId::defend, CardId::burning_pact}) {
        CardHandle selected{};
        for (auto card : state.view().combat->zone(Zone::hand))
            if (state.view().combat->cards.get(card)->definition == definition) { selected = card; break; }
        require(apply_action(state, CombatInput{state.data.boundary, CombatInputKind::play_card, selected}, scratch).status == StepStatus::awaiting_input);
    }
    const auto combat = *state.view().combat;
    require(apply_action(state, CombatInput{state.data.boundary, CombatInputKind::select_card, combat.selection_candidates.front(), false, combat.selection.id}, scratch).status == StepStatus::awaiting_input);
    require(apply_action(state, CombatInput{state.data.boundary, CombatInputKind::confirm_selection, {}, false, combat.selection.id}, scratch).status == StepStatus::awaiting_input);
    require(state.view().combat->data.execution.overflowed());
}

template<class Function> void measure(const char* operation, std::uint64_t iterations, Function function) {
    allocations = 0;
    const auto start = std::chrono::steady_clock::now(); counting = true;
    for (std::uint64_t i = 0; i < iterations; ++i) function();
    counting = false;
    const auto seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::cout << " operation=" << operation << " iterations=" << iterations << " allocations=" << allocations
              << " ns_per_operation=" << seconds * 1e9 / iterations << " operations_per_second=" << iterations / seconds << '\n';
}
int main(int argc, char** argv) try {
    std::uint64_t iterations = 100000;
    if (argc == 2) { char* end = nullptr; iterations = std::strtoull(argv[1], &end, 10); if (!end || *end || !iterations || iterations > 100000000) return 1; }
    else if (argc > 2) return 1;
    std::cout << "run_data_bytes=" << sizeof(RunData) << " combat_data_bytes=" << sizeof(CombatData)
              << " card_instance_bytes=" << sizeof(CombatCard) << " effect_frame_bytes=" << sizeof(EffectFrame)
              << " creature_state_bytes=" << sizeof(CreatureState) << " power_instance_bytes=" << sizeof(PowerInstance) << '\n';
    for (int workload = 0; workload < 5; ++workload) {
        RunState baseline, branch; Scratch scratch;
        if (workload == 4) {
            require(initialize_seeded_combat(baseline, {}).status == StepStatus::awaiting_input);
            auto combat = *baseline.view().combat;
            using Stacking = PowerInstance::Stacking;
            for (int i = 0; i < 9; ++i)
                gameplay::apply_power(combat, scratch, PowerId::strength, 1, false, false, {}, true, false, Stacking::separate, combat.player_handle);
            gameplay::apply_power(combat, scratch, PowerId::strength, 2, false, false, {}, true, false, Stacking::by_applier, combat.player_handle);
            gameplay::apply_power(combat, scratch, PowerId::strength, 3, false, false, {}, true, true, Stacking::by_applier, combat.enemy_handle);
            gameplay::apply_power(combat, scratch, PowerId::strength, 4, false, false);
            require(combat.player.powers.overflowed());
            require(combat.player.power_instances(combat.creature_overflow).size() == 12);
        }
        else if (workload == 3) nested(baseline);
        else if (workload == 2) {
            std::array<CardSpec, 200> deck;
            for (std::size_t i = 0; i < deck.size(); ++i) deck[i] = CardSpec{i % 2 ? CardId::strike : CardId::defend};
            require(initialize_combat_scenario(baseline, {}, deck).status == StepStatus::awaiting_input);
        }
        else {
            require(initialize_seeded_combat(baseline, {}).status == StepStatus::awaiting_input);
            if (workload == 1) for (int i = 0; i < 12; ++i) require(baseline.view().creatures.create({3, 3}, 4096).present());
        }
        require(valid_run(baseline));
        require(clone_into(branch, baseline) == CloneStatus::cloned);
        std::cout << "workload=" << (workload == 0 ? "supported_seeded_combat" : workload == 1 ? "controlled_creature_overflow" : workload == 2 ? "supported_cards_200_overflow" : workload == 3 ? "controlled_nested_overflow" : "controlled_power_instances_12_overflow")
                  << " player_power_instances=" << baseline.view().combat->player.power_instances(baseline.overflow).size()
                  << " root_bytes=" << sizeof(RunState) << " retained_bytes=" << sizeof(RunState) + baseline.overflow.retained_bytes() + baseline.combat_overflow.retained_bytes() << '\n';
        measure("query_validate", iterations, [&] {
            RunActionSpaceView actions(baseline);
            for (std::size_t i = 0; i < actions.size(); ++i) {
                auto action = actions.at(i); require(action.has_value());
                checksum += static_cast<unsigned>(validate_action(baseline, action->input)) + 1;
            }
        });
        measure("clone", iterations, [&] { require(clone_into(branch, baseline) == CloneStatus::cloned); checksum += branch.data.boundary.value; });
        // Clone reset is measured separately; step timing excludes reset while allocation accounting includes only apply_action.
        double step_seconds = 0; std::uint64_t step_allocations = 0;
        for (std::uint64_t i = 0; i < iterations; ++i) {
            require(clone_into(branch, baseline) == CloneStatus::cloned);
            auto input = workload == 3
                ? CombatInput{branch.data.boundary, CombatInputKind::select_card, branch.view().combat->selection_candidates.front(), false, branch.view().combat->selection.id}
                : CombatInput{branch.data.boundary, CombatInputKind::end_turn};
            allocations = 0; counting = true; const auto start = std::chrono::steady_clock::now();
            const auto result = apply_action(branch, input, scratch);
            step_seconds += std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count(); counting = false;
            step_allocations += allocations;
            require(result.status == StepStatus::awaiting_input); checksum += branch.data.boundary.value;
        }
        std::cout << " operation=step iterations=" << iterations << " allocations=" << step_allocations << " ns_per_operation=" << step_seconds * 1e9 / iterations << " operations_per_second=" << iterations / step_seconds << '\n';
    }
    std::cout << "checksum=" << checksum << '\n';
} catch (const std::exception& error) { counting = false; std::cerr << error.what() << '\n'; return 2; }
