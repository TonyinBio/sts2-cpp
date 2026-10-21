#pragma once
#include <array>
#include <bit>
#include <cstdint>
#include <limits>
#include <span>
#include <string_view>
#include <utility>

namespace sts2::engine {
// Pinned v0.111.0: xoshiro256**, SplitMix64 initialization, Rng wrapper count.
struct RngState {
    std::array<std::uint64_t, 4> words{};
    std::uint32_t counter = 0;
    friend bool operator==(const RngState&, const RngState&) = default;
};
class GameRng {
public:
    GameRng() noexcept : GameRng(0) {}
    explicit GameRng(std::uint64_t seed) noexcept;
    [[nodiscard]] const RngState& state() const noexcept { return state_; }
    [[nodiscard]] bool restore(RngState state) noexcept;
    [[nodiscard]] std::uint64_t next_u64() noexcept;
    [[nodiscard]] double next_double() noexcept;
    // Rng.NextBool uses Next(2)==0, not MegaRandom.NextBool's sign bit.
    [[nodiscard]] bool next_bool() noexcept { return next_double() < 0.5; }
    [[nodiscard]] bool next_int(std::int32_t min, std::int32_t max, std::int32_t& result) noexcept;
    [[nodiscard]] bool next_float(float min, float max, float& result) noexcept;
    template<class T> void shuffle(std::span<T> values) noexcept {
        for (std::size_t remaining = values.size(); remaining > 1;) {
            const auto index = static_cast<std::size_t>(next_double() * static_cast<double>(remaining));
            std::swap(values[--remaining], values[index]);
        }
    }
    friend bool operator==(const GameRng&, const GameRng&) = default;
private:
    RngState state_;
};
// StringHelper.GetDeterministicHashCode: xxHash64 of exact UTF-8 bytes, seed zero.
[[nodiscard]] std::uint64_t game_hash(std::string_view utf8) noexcept;
[[nodiscard]] std::uint64_t game_seed(std::string_view utf8) noexcept;
inline constexpr std::array<std::string_view, 12> run_stream_names = {
    "up_front", "shuffle", "unknown_map_point", "combat_card_generation",
    "combat_potion_generation", "combat_card_selection", "combat_energy_costs",
    "combat_targets", "monster_ai", "niche", "combat_orbs", "treasure_room_relics"};
enum class RunStream : std::uint8_t {
    up_front, shuffle, unknown_map_point, combat_card_generation,
    combat_potion_generation, combat_card_selection, combat_energy_costs,
    combat_targets, monster_ai, niche, combat_orbs, treasure_room_relics
};
struct RngBank {
    std::array<GameRng, run_stream_names.size()> streams{};
    explicit RngBank(std::string_view seed = {}) noexcept;
    GameRng& get(RunStream stream) noexcept { return streams[static_cast<std::size_t>(stream)]; }
    const GameRng& get(RunStream stream) const noexcept { return streams[static_cast<std::size_t>(stream)]; }
    friend bool operator==(const RngBank&, const RngBank&) = default;
};
} // namespace sts2::engine
