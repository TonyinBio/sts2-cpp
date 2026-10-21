#include "sts2/state/rng.hpp"
#include <cmath>

namespace sts2::engine {
namespace {
constexpr std::uint64_t p1 = 11400714785074694791ULL, p2 = 14029467366897019727ULL;
constexpr std::uint64_t p3 = 1609587929392839161ULL, p4 = 9650029242287828579ULL;
constexpr std::uint64_t p5 = 2870177450012600261ULL;
std::uint64_t round(std::uint64_t acc, std::uint64_t value) noexcept {
    return std::rotl(acc + value * p2, 31) * p1;
}
std::uint64_t read(std::string_view bytes, std::size_t at, unsigned count) noexcept {
    std::uint64_t result = 0;
    for (unsigned i = 0; i < count; ++i)
        result |= std::uint64_t(static_cast<unsigned char>(bytes[at + i])) << (i * 8);
    return result;
}
std::uint64_t splitmix(std::uint64_t& seed) noexcept {
    auto z = (seed += 0x9e3779b97f4a7c15ULL);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}
}
GameRng::GameRng(std::uint64_t seed) noexcept {
    for (auto& word : state_.words) word = splitmix(seed);
}
bool GameRng::restore(RngState state) noexcept {
    if ((state.words[0] | state.words[1] | state.words[2] | state.words[3]) == 0) return false;
    state_ = state;
    return true;
}
std::uint64_t GameRng::next_u64() noexcept {
    auto& s = state_.words;
    const auto result = std::rotl(s[1] * 5, 7) * 9;
    const auto t = s[1] << 17;
    s[2] ^= s[0]; s[3] ^= s[1]; s[1] ^= s[2]; s[0] ^= s[3];
    s[2] ^= t; s[3] = std::rotl(s[3], 45);
    ++state_.counter; // Defined modulo 2^32, matching the unchecked C# wrapper.
    return result;
}
double GameRng::next_double() noexcept {
    return static_cast<double>(next_u64() >> 11) * 0x1p-53;
}
bool GameRng::next_int(std::int32_t min, std::int32_t max, std::int32_t& result) noexcept {
    if (min >= max) return false;
    const auto range = std::int64_t(max) - min;
    result = static_cast<std::int32_t>(static_cast<std::int64_t>(next_double() * static_cast<double>(range)) + min);
    return true;
}
bool GameRng::next_float(float min, float max, float& result) noexcept {
    if (!std::isfinite(min) || !std::isfinite(max) || min > max || !std::isfinite(max - min)) return false;
    // The game subtracts at float precision, then multiplies/adds at double precision.
    const double scaled = next_double() * static_cast<double>(max - min);
    result = static_cast<float>(scaled + static_cast<double>(min));
    return true;
}
std::uint64_t game_hash(std::string_view bytes) noexcept {
    std::size_t at = 0;
    std::uint64_t hash;
    if (bytes.size() >= 32) {
        std::array<std::uint64_t, 4> v = {p1 + p2, p2, 0, 0 - p1};
        do {
            for (auto& lane : v) { lane = round(lane, read(bytes, at, 8)); at += 8; }
        } while (bytes.size() - at >= 32);
        hash = std::rotl(v[0], 1) + std::rotl(v[1], 7) + std::rotl(v[2], 12) + std::rotl(v[3], 18);
        for (auto lane : v) hash = (hash ^ round(0, lane)) * p1 + p4;
    } else hash = p5;
    hash += bytes.size();
    while (bytes.size() - at >= 8) {
        hash = std::rotl(hash ^ round(0, read(bytes, at, 8)), 27) * p1 + p4; at += 8;
    }
    if (bytes.size() - at >= 4) {
        hash = std::rotl(hash ^ (read(bytes, at, 4) * p1), 23) * p2 + p3; at += 4;
    }
    while (at < bytes.size()) hash = std::rotl(hash ^ (read(bytes, at++, 1) * p5), 11) * p1;
    hash ^= hash >> 33; hash *= p2; hash ^= hash >> 29; hash *= p3;
    return hash ^ (hash >> 32);
}
std::uint64_t game_seed(std::string_view bytes) noexcept {
    if (!bytes.starts_with("old")) return game_hash(bytes);
    // Legacy seeds use unchecked 32-bit hashing of UTF-16 code units. The old
    // seed alphabet is ASCII; non-ASCII legacy seeds are rejected by setup.
    bytes.remove_prefix(3);
    std::uint32_t a = 352654597, b = a;
    for (std::size_t i = 0; i < bytes.size(); i += 2) {
        a = a * 33 ^ static_cast<unsigned char>(bytes[i]);
        if (i + 1 < bytes.size()) b = b * 33 ^ static_cast<unsigned char>(bytes[i + 1]);
    }
    return a + b * 1566083941U;
}
RngBank::RngBank(std::string_view seed) noexcept {
    const auto numeric = game_seed(seed);
    for (std::size_t i = 0; i < streams.size(); ++i) streams[i] = GameRng(numeric + game_hash(run_stream_names[i]));
}
} // namespace sts2::engine
