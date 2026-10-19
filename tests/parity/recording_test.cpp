#include "recording.hpp"
#include <gtest/gtest.h>
#include <fstream>
#include <stdexcept>

namespace sts2::parity {
namespace {
using Json = nlohmann::json;
Json example() {
    std::ifstream input(ACTION_EXAMPLE_PATH);
    return Json::parse(input);
}
ActionRecording parse(const Json& value) {
    return ActionRecording::parse(value, {"basic_combat", "ironclad_starter_shrinker_v1"});
}
TEST(ActionRecordingFormat, PreservesExampleAndActionOrder) {
    const auto value = example();
    EXPECT_EQ(parse(value).json(), value);
    auto reversed = value;
    std::swap(reversed["actions"][0], reversed["actions"][1]);
    EXPECT_EQ(parse(reversed).json(), reversed);
    EXPECT_NE(parse(reversed).json(), value);
}
TEST(ActionRecordingFormat, SupportsBaselineWithNoActions) {
    auto value = example();
    value["actions"] = Json::array();
    EXPECT_NO_THROW(parse(value));
}
TEST(ActionRecordingFormat, SupportsStableIdentityActionsAndOptionalTarget) {
    const std::vector<Json> actions = {
        {{"kind", "play_card"}, {"card_instance", "card_0"}, {"target", "enemy_0"}},
        {{"kind", "play_card"}, {"card_instance", "card_1"}},
        {{"kind", "end_turn"}},
        {{"kind", "select_cards"}, {"card_instances", {"card_2", "card_3"}}},
        {{"kind", "select_cards"}, {"card_instances", Json::array()}},
        {{"kind", "select_target"}, {"target", "enemy_0"}},
    };
    for (const auto& action : actions) {
        SCOPED_TRACE(action.dump());
        auto value = example();
        value["actions"][0]["action"] = action;
        EXPECT_EQ(parse(value).json(), value);
    }
}
TEST(ActionRecordingFormat, RejectsDuplicateActionIdsWithLocation) {
    auto value = example();
    value["actions"][1]["id"] = value["actions"][0]["id"];
    try {
        (void)parse(value);
        FAIL() << "Expected duplicate action ID rejection";
    } catch (const std::invalid_argument& error) {
        EXPECT_NE(std::string(error.what()).find("/actions/1/id"), std::string::npos);
    }
}
TEST(ActionRecordingFormat, RejectsUnsupportedFeatures) {
    auto value = example();
    value["required_features"].push_back("unsupported");
    EXPECT_THROW(parse(value), std::invalid_argument);
}
TEST(ActionRecordingFormat, RejectsMissingAndUnknownEnvelopeFields) {
    const auto original = example();
    for (const auto& [key, unused] : original.items()) {
        SCOPED_TRACE(key);
        auto value = original;
        value.erase(key);
        EXPECT_THROW(parse(value), std::invalid_argument);
    }
    auto value = original;
    value["expected_state"] = Json::object();
    EXPECT_THROW(parse(value), std::invalid_argument);
}
TEST(ActionRecordingFormat, RejectsUnknownProfilesVersionsAndSetup) {
    const std::vector<std::pair<std::string, Json>> changes = {
        {"/schema_version", 2}, {"/schema_version", 1.0},
        {"/profile", "actions-v2"}, {"/state_profile", "unknown"},
        {"/setup/kind", "load_snapshot"}, {"/setup/seed", ""},
        {"/setup/ascension", -1}, {"/setup/ascension", 11},
        {"/setup/character", "unknown"}, {"/setup/encounter", ""},
        {"/actions/0/id", ""}, {"/actions/0/comment", "unknown field"},
        {"/required_features", {"basic_combat", "basic_combat"}},
    };
    for (const auto& [path, replacement] : changes) {
        SCOPED_TRACE(path);
        auto value = example();
        value[Json::json_pointer(path)] = replacement;
        EXPECT_THROW(parse(value), std::invalid_argument);
    }
}
TEST(ActionRecordingFormat, SupportsRunGameplayDecisionsAndRejectsViewCommands) {
    auto value = example();
    value["setup"]["kind"] = "seeded_run";
    value["setup"].erase("encounter");
    const std::vector<Json> actions = {
        {{"kind", "select_map_node"}, {"col", 1}, {"row", 2}},
        {{"kind", "choose_option"}, {"option_index", 0}},
        {{"kind", "select_cards"}, {"indices", Json::array({1, 0})}},
        {{"kind", "select_card_reward"}, {"card_index", 0}},
        {{"kind", "reward_alternative"}, {"index", 0}},
        {{"kind", "select_bundle"}, {"bundle_index", 0}},
        {{"kind", "take_reward"}, {"index", 0}},
        {{"kind", "pick_treasure"}, {"index", 0}},
        {{"kind", "buy_card"}, {"card_index", 0}},
        {{"kind", "buy_relic"}, {"relic_index", 0}},
        {{"kind", "buy_potion"}, {"potion_index", 0}},
        {{"kind", "use_potion"}, {"potion_index", 0}, {"target", "enemy_0"}},
        {{"kind", "discard_potion"}, {"potion_index", 0}},
        {{"kind", "skip_select"}}, {{"kind", "skip_card_reward"}},
        {{"kind", "skip_rewards"}}, {{"kind", "remove_card"}},
        {{"kind", "proceed"}}, {{"kind", "abandon"}}
    };
    for (const auto& action : actions) {
        value["actions"][0]["action"] = action;
        EXPECT_NO_THROW(parse(value)) << action.dump();
        auto broken = value;
        broken["actions"][0]["action"]["unexpected"] = true;
        EXPECT_THROW(parse(broken), std::invalid_argument);
    }
    for (const auto* kind : {"open_map", "map", "state", "view_deck", "inventory", "help", "save", "leave_room"}) {
        value["actions"][0]["action"] = {{"kind", kind}};
        EXPECT_THROW(parse(value), std::invalid_argument) << kind;
    }
    for (const auto& action : std::vector<Json>{
        {{"kind", "select_map_node"}, {"col", 256}, {"row", 1}},
        {{"kind", "choose_option"}, {"option_index", -1}},
        {{"kind", "select_cards"}, {"indices", Json::array({0, 0})}},
        {{"kind", "select_cards"}, {"indices", Json::array({0})}, {"card_instances", Json::array({"card_0"})}},
        {{"kind", "use_potion"}}, {{"kind", "buy_card"}, {"card_index", 0.5}}}) {
        value["actions"][0]["action"] = action;
        EXPECT_THROW(parse(value), std::invalid_argument) << action.dump();
    }
}
class InvalidActionFormat : public testing::TestWithParam<Json> {};
TEST_P(InvalidActionFormat, RejectsMalformedAction) {
    auto value = example();
    value["actions"][0]["action"] = GetParam();
    EXPECT_THROW(parse(value), std::invalid_argument);
}
INSTANTIATE_TEST_SUITE_P(ActionRecording, InvalidActionFormat, testing::Values(
    Json{{"kind", "unknown"}},
    Json{{"kind", "play_card"}},
    Json{{"kind", "play_card"}, {"card_instance", ""}},
    Json{{"kind", "play_card"}, {"card_index", 0}},
    Json{{"kind", "play_card"}, {"card_instance", "card_0"}, {"target", nullptr}},
    Json{{"kind", "end_turn"}, {"target", "enemy_0"}},
    Json{{"kind", "select_cards"}, {"card_instances", {"card_0", "card_0"}}},
    Json{{"kind", "select_cards"}, {"card_instances", {0}}},
    Json{{"kind", "select_target"}},
    Json::array(), Json(nullptr)));
} // namespace
} // namespace sts2::parity
