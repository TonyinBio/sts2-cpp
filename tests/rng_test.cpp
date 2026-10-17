#include "sts2/rng.hpp"
#include <gtest/gtest.h>

namespace sts2 {
TEST(RngTest, Xoshiro256StarStarKnownVector) {
    auto rng = Rng::seeded(0);
    EXPECT_EQ(rng.words[0], UINT64_C(0xe220a8397b1dcdaf));
    EXPECT_EQ(rng.next(), UINT64_C(11091344671253066420));
    EXPECT_EQ(rng.next(), UINT64_C(13793997310169335082));
    EXPECT_EQ(rng.next(), UINT64_C(1900383378846508768));
    EXPECT_EQ(rng.counter, 3u);
}
TEST(RngTest, CopyRestoresWordsAndCounter) {
    auto rng = Rng::seeded(99);
    (void)rng.next();
    auto copied = rng;
    for (int i = 0; i < 16; ++i) EXPECT_EQ(rng.next(), copied.next());
    EXPECT_EQ(rng, copied);
}
TEST(RngTest, BoundedDrawUsesReferenceDoubleScaling) {
    auto bounded = Rng::seeded(3), raw = bounded;
    for (int i = 1; i < 100; ++i) {
        const auto unit = static_cast<double>(raw.next() >> 11) * 0x1.0p-53;
        EXPECT_EQ(bounded.next_int(i), static_cast<int>(unit * i));
    }
    EXPECT_EQ(bounded, raw);
}
}
