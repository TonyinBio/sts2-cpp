#include "sts2/parity/state_recording.hpp"
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
TEST(StateRecording, ValidatesEnvelopeAndRunParitySchema) { EXPECT_NO_THROW(validate(example())); }
TEST(StateRecording, AcceptsSparseAndInitialOnlyCheckpoints) {
    auto value = example(); value["transitions"].erase(0);
    EXPECT_NO_THROW(validate(value));
    value = example(); value["transitions"] = Json::array({value["transitions"].back()});
    EXPECT_NO_THROW(validate(value));
    value["transitions"] = Json::array();
    EXPECT_NO_THROW(validate(value));
}
TEST(StateRecording, AcceptsCheckpointsAfterFiveAndTenActions) {
    auto input = actions(); input["actions"] = Json::array();
    for (int i = 1; i <= 12; ++i)
        input["actions"].push_back({{"id", "action-" + std::to_string(i)}, {"action", {{"kind", "end_turn"}}}});
    auto value = example();
    const auto state = value["initial"];
    value["transitions"] = Json::array({
        {{"action_id", "action-5"}, {"state", state}},
        {{"action_id", "action-10"}, {"state", state}}});
    EXPECT_NO_THROW(validate_state_recording(value, ActionRecording::parse(input, features), features));
}
TEST(StateRecording, RejectsUnknownDuplicateOrReorderedCheckpoints) {
    auto value = example(); value["transitions"][0]["action_id"] = "unknown";
    EXPECT_THROW(validate(value), std::invalid_argument);
    value = example(); value["transitions"].push_back(value["transitions"][0]);
    EXPECT_THROW(validate(value), std::invalid_argument);
    value = example(); value["transitions"][1]["action_id"] = value["transitions"][0]["action_id"];
    EXPECT_THROW(validate(value), std::invalid_argument);
    value = example(); std::swap(value["transitions"][0], value["transitions"][1]);
    EXPECT_THROW(validate(value), std::invalid_argument);
}
TEST(StateRecording, RejectsInvalidRunSnapshot) {
    auto value = example(); value["initial"]["combat"]["players"][0]["piles"]["Hand"].push_back("/combat/cards/unknown");
    EXPECT_THROW(validate(value), std::invalid_argument);
    value = example(); value["initial"]["profile"] = "old";
    EXPECT_THROW(validate(value), std::invalid_argument);
}
TEST(StateRecording, RejectsActionsAfterTerminalState) {
    auto value = example();
    auto& state = value["transitions"][0]["state"];
    state["boundary_kind"] = "run_complete";
    state["run_ending_reason"] = "victory";
    state["pending_choice"] = nullptr;
    EXPECT_THROW(validate(value), std::invalid_argument);
}
TEST(StateRecording, RejectsUnrecordedActionsAfterTerminalCheckpoint) {
    auto value = example();
    auto step = value["transitions"][0];
    step["state"]["boundary_kind"] = "run_complete";
    step["state"]["run_ending_reason"] = "death";
    step["state"]["pending_choice"] = nullptr;
    value["transitions"] = Json::array({step});
    EXPECT_THROW(validate(value), std::invalid_argument);
    value["transitions"][0]["action_id"] = actions()["actions"].back()["id"];
    EXPECT_NO_THROW(validate(value));
}
TEST(StateRecording, TerminalInitialStateRequiresNoActions) {
    auto value = example();
    value["transitions"] = Json::array();
    value["initial"]["boundary_kind"] = "run_complete";
    value["initial"]["run_ending_reason"] = "victory";
    value["initial"]["pending_choice"] = nullptr;
    EXPECT_THROW(validate(value), std::invalid_argument);
    auto input = actions(); input["actions"] = Json::array();
    EXPECT_NO_THROW(validate_state_recording(value, ActionRecording::parse(input, features), features));
}
TEST(StateRecording, AllowsDocumentLocalLabelsAcrossBoundaries) {
    auto value = example();
    EXPECT_NO_THROW(validate(value));
    value["transitions"][0]["state"]["combat"]["cards"]["survivor"]["saved"]["id"] = "CARD.OTHER";
    // A local label is not a persistent instance identity across snapshots.
    EXPECT_NO_THROW(validate(value));
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
