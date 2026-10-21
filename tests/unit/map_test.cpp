#include "sts2/state/map.hpp"
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <fstream>

namespace sts2::engine {
namespace {
using Json = nlohmann::json;
Json projection(const MapState& m, MapNodeId id) {
    const auto& n = m.nodes[id]; Json children = Json::array();
    for (auto c : n.children.values()) children.push_back({{"col", m.nodes[c].col}, {"row", m.nodes[c].row}});
    return {{"col", n.col}, {"row", n.row}, {"kind", static_cast<int>(n.kind)}, {"can_modify", n.can_modify}, {"children", children}};
}
TEST(MapGeneration, MatchesAllNativeCoordinatesKindsEdgesAndFinalRngState) {
    std::ifstream stream(KERNEL_MAP_FIXTURE); const auto fixture = Json::parse(stream);
    for (const auto& test : fixture["cases"]) {
        SCOPED_TRACE(test["act"].get<std::string>() + " seed=" + test["numeric_seed"].dump() + " A" + test["ascension"].dump());
        MapState map;
        ASSERT_TRUE(generate_map(map, test["act"].get<std::string>(), test["numeric_seed"].get<std::uint64_t>(), test["ascension"].get<std::uint8_t>()));
        ASSERT_TRUE(valid_map(map));
        Json points = Json::array(); for (auto id : map.points()) points.push_back(projection(map, id));
        EXPECT_EQ(points, test["expected"]["points"]);
        EXPECT_EQ(projection(map, map.start), test["expected"]["start"]);
        EXPECT_EQ(projection(map, map.boss), test["expected"]["boss"]);
        EXPECT_EQ(map.second_boss == no_node ? Json(nullptr) : projection(map, map.second_boss), test["expected"]["second_boss"]);
        EXPECT_EQ(map.generator_state.counter, test["expected"]["rng"]["counter"]);
        EXPECT_EQ(Json(map.generator_state.words), test["expected"]["rng"]["words"]);
    }
}
TEST(MapGeneration, InvalidSetupPreservesPriorMapAndClonesAreIndependent) {
    MapState map; ASSERT_TRUE(generate_map(map, "ACT.OVERGROWTH", 123));
    const auto before = map;
    EXPECT_FALSE(generate_map(map, "unknown", 123)); EXPECT_EQ(map, before);
    EXPECT_FALSE(generate_map(map, "ACT.OVERGROWTH", 123, 11)); EXPECT_EQ(map, before);
    auto clone = map; clone.nodes[clone.start].kind = MapKind::monster;
    EXPECT_EQ(map.nodes[map.start].kind, MapKind::ancient); EXPECT_NE(map, clone);
}
TEST(MapEdges, RemovalAndReinsertionMatchHashSetSlotReuse) {
    MapEdges edges; EXPECT_TRUE(edges.add(1)); EXPECT_TRUE(edges.add(2)); EXPECT_TRUE(edges.add(3));
    EXPECT_TRUE(edges.remove(2)); EXPECT_TRUE(edges.add(4));
    EXPECT_EQ(edges.values(), (std::vector<MapNodeId>{1, 4, 3}));
    EXPECT_FALSE(edges.add(4)); EXPECT_FALSE(edges.remove(99));
}
}
}
