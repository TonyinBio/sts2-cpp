#include "sts2/rng.hpp"
#include <gtest/gtest.h>

namespace sts2 {
namespace {

TEST(RngTest, MatchesSplitMix64KnownVector) {
    Rng rng{0};
    EXPECT_EQ(rng.next(), UINT64_C(0xe220a8397b1dcdaf));
}

TEST(RngTest, CopyPreservesSequence) {
    Rng rng{0};
    (void)rng.next();
    auto copied = rng;
    for (int i = 0; i < 16; ++i) {
        EXPECT_EQ(rng.next(), copied.next()) << "draw " << i;
    }
}

} // namespace
} // namespace sts2
