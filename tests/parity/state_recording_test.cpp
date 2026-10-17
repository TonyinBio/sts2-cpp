#include "state_recording.hpp"
#include <gtest/gtest.h>
#include <fstream>

namespace sts2::parity {
namespace {
using Json = nlohmann::json;
const std::set<std::string> features = {"basic_combat", "ironclad_starter_shrinker_v1"};
Json read(const char* path) { std::ifstream stream(path); return Json::parse(stream); }
Json actions() { return read(ACTION_EXAMPLE_PATH); }
Json example() {
    auto state = read(PARITY_EXAMPLE_PATH);
    state["required_features"] = actions()["required_features"];
    Json transitions = Json::array();
    const auto input = actions();
    for (const auto& action : input["actions"])
        transitions.push_back({{"action_id", action["id"]}, {"state", state}});
    return {{"schema_version", 1}, {"profile", "states-v1"},
        {"recording_id", actions()["recording_id"]}, {"actions_sha256", std::string(64, 'a')},
        {"reference", {{"revision", std::string(40, 'b')}, {"game_dll_sha256", std::string(64, 'c')},
            {"adapter_sha256", std::string(64, 'd')}, {"assembly_mvid", "example"}, {"runtime_version", "9.0.4"}}},
        {"initial", state}, {"transitions", transitions}};
}
void validate(const Json& value) {
    validate_state_recording(value, ActionRecording::parse(actions(), features), features);
}
TEST(StateRecording, ValidatesEnvelopeAndExistingCombatSchema) { EXPECT_NO_THROW(validate(example())); }
TEST(StateRecording, RejectsMissingExtraOrReorderedTransitions) {
    auto value = example(); value["transitions"].erase(0);
    EXPECT_THROW(validate(value), std::invalid_argument);
    value = example(); value["transitions"].push_back(value["transitions"][0]);
    EXPECT_THROW(validate(value), std::invalid_argument);
    value = example(); std::swap(value["transitions"][0], value["transitions"][1]);
    EXPECT_THROW(validate(value), std::invalid_argument);
}
TEST(StateRecording, RejectsInvalidSnapshotAndFeatureMismatch) {
    auto value = example(); value["initial"]["piles"]["hand"].push_back("unknown");
    EXPECT_THROW(validate(value), std::invalid_argument);
    value = example(); value["transitions"][0]["state"]["required_features"] = {"basic_combat"};
    EXPECT_THROW(validate(value), std::invalid_argument);
}
TEST(StateRecording, RejectsActionsAfterTerminalState) {
    auto value = example();
    auto& state = value["transitions"][0]["state"];
    state["boundary"] = "combat_end";
    state["combat"]["active_side"] = "none";
    state["combat"]["outcome"] = "victory";
    EXPECT_THROW(validate(value), std::invalid_argument);
}
TEST(StateRecording, RejectsChangedCardIdentity) {
    auto value = example(); value["transitions"][0]["state"]["cards"][0]["model_id"] = "CARD.OTHER";
    EXPECT_THROW(validate(value), std::invalid_argument);
}
TEST(StateRecording, RejectsInvalidProvenance) {
    auto value = example(); value["actions_sha256"] = "not-a-hash";
    EXPECT_THROW(validate(value), std::invalid_argument);
    value = example(); value["recording_id"] = "other";
    EXPECT_THROW(validate(value), std::invalid_argument);
    value = example(); value["reference"]["unknown"] = "field";
    EXPECT_THROW(validate(value), std::invalid_argument);
}
}
}
