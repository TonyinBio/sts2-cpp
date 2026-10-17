#include "sts2/simulator.hpp"
#include <gtest/gtest.h>
#include <cstdlib>
#include <new>

namespace {
thread_local bool counting = false;
thread_local std::size_t allocations = 0;
}
void* operator new(std::size_t size) {
    if (counting) ++allocations;
    if (auto* p = std::malloc(size ? size : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

TEST(SimulationMemory, SetupBranchCopiesAndRolloutsDoNotAllocate) {
    // Keep assertions/GoogleTest outside the measured interval.
    allocations = 0;
    bool valid = true;
    std::uint64_t checksum = 0;
    counting = true;
    for (std::uint64_t i = 0; i < 128; ++i) {
        const auto initial = sts2::make_starter_combat(i);
        auto state = initial;
        for (int n = 0; n < 100 && state.outcome == sts2::Outcome::ongoing; ++n) {
            const auto actions = sts2::legal_actions(state);
            valid &= sts2::step(state, actions.actions[0]);
        }
        checksum += state.player_hp;
        valid &= state.outcome != sts2::Outcome::ongoing;
        valid &= initial.turn == 1;
    }
    counting = false;
    EXPECT_TRUE(valid);
    EXPECT_GT(checksum, 0u);
    EXPECT_EQ(allocations, 0u);
    EXPECT_LE(sizeof(sts2::CombatState), 256u);
}
