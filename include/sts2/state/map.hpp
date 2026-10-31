#pragma once
#include "sts2/core/rng.hpp"
#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

namespace sts2::engine {
enum class MapKind : std::uint8_t { unassigned, unknown, shop, treasure, rest, monster, elite, boss, ancient };
using MapNodeId = std::uint16_t;
inline constexpr MapNodeId no_node = UINT16_MAX;
// Reproduces .NET HashSet's observable slot enumeration, including LIFO reuse
// of removed slots. Map edge order affects path pruning and later serialization.
struct MapEdges {
    std::array<MapNodeId, 7> slots{};
    std::array<std::uint8_t, 7> free{};
    std::uint8_t used = 0, free_count = 0;
    MapEdges() noexcept { slots.fill(no_node); }
    [[nodiscard]] bool contains(MapNodeId) const noexcept;
    bool add(MapNodeId);
    bool remove(MapNodeId) noexcept;
    [[nodiscard]] std::size_t size() const noexcept { return used - free_count; }
    [[nodiscard]] std::vector<MapNodeId> values() const;
    friend bool operator==(const MapEdges&, const MapEdges&) = default;
};
struct MapNode {
    std::int32_t col = 0, row = 0;
    MapKind kind = MapKind::unassigned;
    bool can_modify : 1 = true;
    bool active : 1 = false;
    MapEdges children, parents;
    friend bool operator==(const MapNode&, const MapNode&) = default;
};
struct MapState {
    static constexpr int width = 7, max_height = 16;
    static constexpr std::size_t max_nodes = width * max_height + 3;
    std::array<MapNode, max_nodes> nodes{};
    std::array<MapNodeId, width * max_height> grid{};
    std::uint16_t count = 0;
    std::int32_t height = 0;
    MapNodeId start = no_node, boss = no_node, second_boss = no_node;
    MapEdges starts;
    RngState generator_state;
    MapState() noexcept { grid.fill(no_node); }
    [[nodiscard]] MapNodeId at(int col, int row) const noexcept;
    [[nodiscard]] std::vector<MapNodeId> points() const;
    friend bool operator==(const MapState&, const MapState&) = default;
};
// Native standard maps only: all four non-deprecated acts, one player, A0..A10.
// Numeric seed is the run seed, not an expected generator state.
[[nodiscard]] bool generate_map(MapState&, std::string_view act, std::uint64_t numeric_seed,
                                 std::uint8_t ascension = 0);
[[nodiscard]] bool valid_map(const MapState&) noexcept;
} // namespace sts2::engine
