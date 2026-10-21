#include "sts2/state/clock.hpp"
#include <gtest/gtest.h>

namespace sts2 {
TEST(RunClock, ActionClockContractAndIndependentCopies) {
    RunClock clock;
    ASSERT_TRUE(clock.initialize({1700000000, 7}));
    EXPECT_EQ(clock.now_seconds(), 1700000000);
    EXPECT_EQ(clock.elapsed_seconds(), 0);
    ASSERT_TRUE(clock.advance_action());
    auto branch = clock;
    ASSERT_TRUE(branch.advance_action());
    EXPECT_EQ(clock.now_seconds(), 1700000007);
    EXPECT_EQ(branch.now_seconds(), 1700000014);
    EXPECT_EQ(branch.elapsed_seconds(), 14);
}
TEST(RunClock, InvalidConfigurationAndExhaustionAreAtomic) {
    RunClock clock;
    ASSERT_TRUE(clock.initialize({RunClock::max_seconds - 1, 1}));
    ASSERT_TRUE(clock.advance_action());
    const auto before = clock;
    EXPECT_FALSE(clock.advance_action());
    EXPECT_EQ(clock, before);
    for (auto config : {ClockConfig{-1, 1}, ClockConfig{RunClock::max_seconds + 1, 1},
                        ClockConfig{0, -1}, ClockConfig{0, 86401}}) {
        EXPECT_FALSE(clock.initialize(config));
        EXPECT_EQ(clock, before);
    }
    ASSERT_TRUE(clock.initialize({RunClock::max_seconds, 0}));
    EXPECT_TRUE(clock.advance_action());
    EXPECT_EQ(clock.elapsed_seconds(), 0);
}
} // namespace sts2
