#pragma once
#include <array>
#include <bit>
#include <cstdint>

namespace sts2 {
// v0.111.0 MegaRandom: SplitMix64 initialization, xoshiro256** output.
// Counter measures public Rng draws. Unsigned overflow is intentional.
struct Rng {
    std::array<std::uint64_t, 4> words{};
    std::uint64_t counter = 0;

    static constexpr Rng seeded(std::uint64_t seed) noexcept {
        Rng result;
        for (auto& word : result.words) {
            auto z = (seed += UINT64_C(0x9e3779b97f4a7c15));
            z = (z ^ (z >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
            z = (z ^ (z >> 27)) * UINT64_C(0x94d049bb133111eb);
            word = z ^ (z >> 31);
        }
        return result;
    }
    [[nodiscard]] constexpr std::uint64_t next() noexcept {
        const auto result = std::rotl(words[1] * 5, 7) * 9;
        const auto t = words[1] << 17;
        words[2] ^= words[0];
        words[3] ^= words[1];
        words[1] ^= words[2];
        words[0] ^= words[3];
        words[2] ^= t;
        words[3] = std::rotl(words[3], 45);
        ++counter;
        return result;
    }
    // Precondition: exclusive_max > 0, matching the game's NextInt(max).
    [[nodiscard]] int next_int(int exclusive_max) noexcept {
        const double unit = static_cast<double>(next() >> 11) * 0x1.0p-53;
        return static_cast<int>(unit * exclusive_max);
    }
    bool operator==(const Rng&) const = default;
};
} // namespace sts2
