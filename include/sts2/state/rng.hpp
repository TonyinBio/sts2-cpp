#pragma once
#include "sts2/core/rng.hpp"
#include <array>
#include <string_view>

namespace sts2::engine {
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
    explicit RngBank(std::string_view seed = {}) noexcept {
        const auto numeric = game_seed(seed);
        for (std::size_t i = 0; i < streams.size(); ++i) streams[i] = GameRng(numeric + game_hash(run_stream_names[i]));
    }
    GameRng& get(RunStream stream) noexcept { return streams[static_cast<std::size_t>(stream)]; }
    const GameRng& get(RunStream stream) const noexcept { return streams[static_cast<std::size_t>(stream)]; }
    friend bool operator==(const RngBank&, const RngBank&) = default;
};
struct PlayerRngBank {
    std::array<GameRng, 3> streams;
    explicit PlayerRngBank(std::string_view seed = {}) noexcept {
        constexpr std::array<std::string_view, 3> names{"rewards", "shops", "transformations"};
        for (std::size_t i = 0; i < streams.size(); ++i) streams[i] = GameRng(game_hash(seed) + game_hash(names[i]));
    }
    friend bool operator==(const PlayerRngBank&, const PlayerRngBank&) = default;
};
} // namespace sts2::engine
