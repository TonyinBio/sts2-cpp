#include "snapshot.hpp"
#include <gtest/gtest.h>
#include <fstream>

namespace sts2::parity {
namespace {
using Json = nlohmann::json;
Json example() {
    std::ifstream input(PARITY_EXAMPLE_PATH);
    return Json::parse(input);
}
Snapshot parse(const Json& value) { return Snapshot::parse(value, {"basic_combat"}); }

TEST(CombatStateFormat, AcceptsExampleAndMatchesIdenticalState) {
    const auto value = parse(example());
    EXPECT_TRUE(compare(value, value).empty());
}
TEST(CombatStateFormat, ReportsExactFieldAndValues) {
    auto value = example();
    value["player"]["hp"] = 72;
    const auto diff = compare(parse(example()), parse(value));
    ASSERT_EQ(diff.size(), 1u);
    EXPECT_EQ(diff[0].path, "/player/hp");
    EXPECT_EQ(diff[0].expected, 80);
    EXPECT_EQ(diff[0].actual, 72);
}
TEST(CombatStateFormat, PreservesPileOrderAndInstanceIdentity) {
    auto value = example();
    std::swap(value["piles"]["hand"][0], value["piles"]["hand"][1]);
    const auto diff = compare(parse(example()), parse(value));
    ASSERT_EQ(diff.size(), 2u);
    EXPECT_EQ(diff[0].path, "/piles/hand/0");
}
TEST(CombatStateFormat, PreservesCardInventoryOrder) {
    auto value = example();
    std::swap(value["cards"][0], value["cards"][1]);
    EXPECT_EQ(parse(value).json(), value);
    const auto diff = compare(parse(example()), parse(value));
    ASSERT_FALSE(diff.empty());
    EXPECT_EQ(diff[0].path, "/cards/0/instance_id");
}
TEST(CombatStateFormat, PreservesFlagOrder) {
    auto expected = example();
    expected["cards"][0]["flags"] = {"a", "b"};
    auto actual = expected;
    actual["cards"][0]["flags"] = {"b", "a"};
    EXPECT_EQ(parse(actual).json(), actual);
    const auto diff = compare(parse(expected), parse(actual));
    ASSERT_EQ(diff.size(), 2u);
    EXPECT_EQ(diff[0].path, "/cards/0/flags/0");
}
TEST(CombatStateFormat, PreservesFeatureDeclarationOrder) {
    auto expected = example();
    expected["required_features"] = {"basic_combat", "powers"};
    auto actual = expected;
    actual["required_features"] = {"powers", "basic_combat"};
    const std::set<std::string> supported = {"basic_combat", "powers"};
    const auto parsed = Snapshot::parse(actual, supported);
    EXPECT_EQ(parsed.json(), actual);
    const auto diff = compare(Snapshot::parse(expected, supported), parsed);
    ASSERT_EQ(diff.size(), 2u);
    EXPECT_EQ(diff[0].path, "/required_features/0");
}
TEST(CombatStateFormat, PreservesPowerOrder) {
    auto expected = example();
    expected["player"]["powers"] = Json::array({
        {{"instance_id", "power_0"}, {"model_id", "POWER.A"}, {"amount", 1}, {"counters", Json::object()}},
        {{"instance_id", "power_1"}, {"model_id", "POWER.B"}, {"amount", 1}, {"counters", Json::object()}}});
    auto actual = expected;
    std::swap(actual["player"]["powers"][0], actual["player"]["powers"][1]);
    EXPECT_FALSE(compare(parse(expected), parse(actual)).empty());
}
TEST(CombatStateFormat, ComparesRngWordsWithoutPrecisionLoss) {
    auto value = example();
    value["randomness"]["run"]["Shuffle"]["s3"] = "18446744073709551614";
    const auto diff = compare(parse(example()), parse(value));
    ASSERT_EQ(diff.size(), 1u);
    EXPECT_EQ(diff[0].path, "/randomness/run/Shuffle/s3");
    EXPECT_EQ(diff[0].expected, "18446744073709551615");
}
TEST(CombatStateFormat, RequiresRngStateWithoutModeSelector) {
    auto value = example();
    value["randomness"]["mode"] = "obsolete";
    EXPECT_THROW(parse(value), std::invalid_argument);
    value = example();
    value["randomness"].erase("run");
    EXPECT_THROW(parse(value), std::invalid_argument);
    value = example();
    value["randomness"].erase("player");
    EXPECT_THROW(parse(value), std::invalid_argument);
}

class InvalidSnapshot : public testing::TestWithParam<std::pair<std::string, Json>> {};
TEST_P(InvalidSnapshot, RejectsUnsupportedOrIncompleteState) {
    auto value = example();
    const auto& [pointer, replacement] = GetParam();
    value[Json::json_pointer(pointer)] = replacement;
    EXPECT_THROW(parse(value), std::invalid_argument) << pointer;
}
INSTANTIATE_TEST_SUITE_P(Schema, InvalidSnapshot, testing::Values(
    std::pair<std::string, Json>{"/schema_version", 2},
    std::pair<std::string, Json>{"/schema_version", 1.0},
    std::pair<std::string, Json>{"/profile", "unknown"},
    std::pair<std::string, Json>{"/required_features", Json::array({"unimplemented_card"})},
    std::pair<std::string, Json>{"/player/hp", -1},
    std::pair<std::string, Json>{"/player/hp", 81},
    std::pair<std::string, Json>{"/player/energy", 2.5},
    std::pair<std::string, Json>{"/player/counters", {{"foo", 1.5}}},
    std::pair<std::string, Json>{"/player/extra", 1},
    std::pair<std::string, Json>{"/randomness/run/Shuffle/s0", "18446744073709551616"},
    std::pair<std::string, Json>{"/randomness/run/Shuffle/s0", "01"},
    std::pair<std::string, Json>{"/randomness/run/Shuffle/s0", 42},
    std::pair<std::string, Json>{"/cards/1/instance_id", "card_0"},
    std::pair<std::string, Json>{"/piles/draw", Json::array({"card_0"})},
    std::pair<std::string, Json>{"/piles/hand", Json::array({"missing"})},
    std::pair<std::string, Json>{"/piles/hand", Json::array({"card_0"})},
    std::pair<std::string, Json>{"/boundary", "combat_end"},
    std::pair<std::string, Json>{"/combat/active_side", "enemy"},
    std::pair<std::string, Json>{"/selection/kind", "card"},
    std::pair<std::string, Json>{"/cards/0/cost", {{"kind", "x"}, {"amount", 1}}}
));
TEST(CombatStateFormat, MissingCounterIsDifferentFromExplicitZero) {
    auto value = example();
    value["player"]["counters"]["cards_played"] = 0;
    const auto diff = compare(parse(example()), parse(value));
    ASSERT_EQ(diff.size(), 1u);
    EXPECT_EQ(diff[0].path, "/player/counters/cards_played");
    EXPECT_TRUE(diff[0].expected.is_null());
    EXPECT_EQ(diff[0].actual, 0);
}
TEST(CombatStateFormat, TargetSelectionsRequireCreatureIdentities) {
    auto value = example();
    value["boundary"] = "selection";
    value["selection"] = {{"kind", "target"}, {"options", {"enemy_0"}}, {"min", 1}, {"max", 1}};
    EXPECT_NO_THROW(parse(value));
    value["selection"]["options"] = {"card_0"};
    EXPECT_THROW(parse(value), std::invalid_argument);
}
TEST(CombatStateFormat, MissingFieldsDoNotReceiveDefaults) {
    auto value = example();
    value["randomness"]["run"].erase("Shuffle");
    EXPECT_THROW(parse(value), std::invalid_argument);
    value = example();
    value["player"].erase("powers");
    try { (void)parse(value); FAIL() << "Accepted missing powers"; }
    catch (const std::invalid_argument& error) {
        EXPECT_NE(std::string(error.what()).find("/player/powers"), std::string::npos);
    }
}
TEST(CombatStateFormat, ValidatesSelectionsAndTerminalBoundaries) {
    auto value = example();
    value["boundary"] = "selection";
    value["selection"] = {{"kind", "card"}, {"options", {"card_0"}}, {"min", 1}, {"max", 1}};
    EXPECT_NO_THROW(parse(value));
    value["selection"]["options"] = {"missing"};
    EXPECT_THROW(parse(value), std::invalid_argument);
    value = example();
    value["boundary"] = "combat_end";
    value["combat"]["outcome"] = "victory";
    value["combat"]["active_side"] = "none";
    EXPECT_NO_THROW(parse(value));
}
} // namespace
} // namespace sts2::parity
