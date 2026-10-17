#pragma once
#include <cstdint>

namespace sts2 {
// SplitMix64 for local experiments only. Not the game's RNG.
// Explicit state makes copying simulation branches reproducible.
struct Rng {
    std::uint64_t state;
    [[nodiscard]] std::uint64_t next() noexcept {
        auto z = (state += UINT64_C(0x9e3779b97f4a7c15));
        z = (z ^ (z >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
        z = (z ^ (z >> 27)) * UINT64_C(0x94d049bb133111eb);
        return z ^ (z >> 31);
    }
};
} // namespace sts2
