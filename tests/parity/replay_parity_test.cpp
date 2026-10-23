#include "sts2/parity/replay.hpp"
#include "sts2/parity/simulator_snapshot.hpp"
#include <gtest/gtest.h>
#include <openssl/evp.h>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace sts2::parity {
namespace {
using Json = nlohmann::json;
const std::set<std::string> features = {"basic_combat", "ironclad_starter_shrinker_v1"};
std::string read(const char* path) {
    std::ifstream stream(path);
    if (!stream) throw std::runtime_error(std::string("Cannot read ") + path);
    return {std::istreambuf_iterator<char>(stream), {}};
}
std::string digest(const std::string& bytes) {
    unsigned char hash[EVP_MAX_MD_SIZE]; unsigned int count = 0;
    if (EVP_Digest(bytes.data(), bytes.size(), hash, &count, EVP_sha256(), nullptr) != 1)
        throw std::runtime_error("SHA-256 failed");
    std::ostringstream out;
    for (unsigned int i = 0; i < count; ++i)
        out << std::hex << std::setw(2) << std::setfill('0') << unsigned(hash[i]);
    return out.str();
}
class ReplayParity : public testing::Test {
protected:
    std::string input = read(ACTION_EXAMPLE_PATH);
    Json states = Json::parse(read(STATE_FIXTURE_PATH));
    Json reference = states.at("reference");
    Json actual = states.at("initial");
    std::vector<Json> executed;
    int initialized = 0;
    ReplayBackend backend{
        [&](const Json& setup, const Snapshot&) { EXPECT_EQ(setup, Json::parse(input)["setup"]); ++initialized; },
        [&](const Json& action) { executed.push_back(action); },
        [&] { return Snapshot::parse(actual); }};
    auto run() { return replay_parity(input, states.dump(), features, reference, backend); }
    void constant_checkpoints() {
        for (auto& entry : states["transitions"]) entry["state"] = actual;
    }
};
TEST_F(ReplayParity, ExecutesAllActionsAndComparesEveryCheckpoint) {
    // A scripted backend tests orchestration, not simulator fidelity.
    std::size_t position = 0;
    backend.apply = [&](const Json& action) {
        executed.push_back(action);
        actual = states["transitions"][position++]["state"];
    };
    EXPECT_TRUE(run().empty());
    EXPECT_EQ(initialized, 1);
    EXPECT_EQ(executed.size(), Json::parse(input)["actions"].size());
}
TEST_F(ReplayParity, ReportsFirstDivergenceWithActionAndExactField) {
    constant_checkpoints();
    backend.apply = [&](const Json& action) {
        executed.push_back(action);
        actual["players"][0]["current_hp"] = 1;
    };
    const auto result = run();
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0].checkpoint, "defend-1");
    ASSERT_EQ(result[0].differences.size(), 1u);
    EXPECT_EQ(result[0].differences[0].path, "/players/0/current_hp");
    EXPECT_EQ(result[0].differences[0].actual, 1);
    EXPECT_EQ(executed.size(), 1u);
}
TEST_F(ReplayParity, DetectsInitialMismatchBeforeExecutingActions) {
    actual["players"][0]["current_hp"] = 1;
    const auto result = run();
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0].checkpoint, "initial");
    EXPECT_TRUE(executed.empty());
}
TEST_F(ReplayParity, SparseCheckpointsStillExecuteEveryAction) {
    constant_checkpoints();
    states["transitions"] = Json::array({states["transitions"].back()});
    EXPECT_TRUE(run().empty());
    EXPECT_EQ(executed.size(), 4u);
    states["transitions"] = Json::array();
    executed.clear();
    EXPECT_TRUE(run().empty());
    EXPECT_EQ(executed.size(), 4u);
}
TEST_F(ReplayParity, RejectsDigestProvenanceAndSchemaBeforeInitialization) {
    const auto original = states;
    states["actions_sha256"] = std::string(64, '0');
    EXPECT_THROW(run(), std::invalid_argument);
    states = original;
    reference["revision"] = std::string(40, '0');
    EXPECT_THROW(run(), std::invalid_argument);
    reference = original["reference"];
    states["initial"].erase("combat");
    EXPECT_THROW(run(), std::invalid_argument);
    EXPECT_EQ(initialized, 0);
}
TEST_F(ReplayParity, ActionBytesAreHashedWithoutNormalization) {
    input += '\n';
    EXPECT_THROW(run(), std::invalid_argument);
    states["actions_sha256"] = digest(input);
    constant_checkpoints();
    EXPECT_TRUE(run().empty());
}
TEST_F(ReplayParity, InvalidActionStopsAtItsIdentity) {
    backend.apply = [&](const Json&) { throw std::invalid_argument("Unknown card identity"); };
    try { (void)run(); FAIL() << "Expected replay failure"; }
    catch (const std::runtime_error& error) {
        EXPECT_NE(std::string(error.what()).find("defend-1: Unknown card identity"), std::string::npos);
    }
}
TEST_F(ReplayParity, ExactDecimalDifferencesSurviveEnvelopeParsing) {
    constant_checkpoints();
    actual["combat"]["cards"]["card_0"]["dynamic_vars"]["Block"] = 0.12345678901234568;
    states["initial"] = actual;
    auto wire = states.dump();
    auto position = wire.find("0.12345678901234568");
    ASSERT_NE(position, std::string::npos);
    wire.replace(position, 19, "0.123456789012345679");
    const auto result = replay_parity(input, wire, features, reference, backend);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0].checkpoint, "initial");
    EXPECT_FALSE(result[0].differences.empty());
}
TEST_F(ReplayParity, LoadsAndExportsRealInitialState) {
    SimulatorSnapshot simulator;
    const auto initial = Snapshot::parse(states["initial"]);
    simulator.load(initial);
    EXPECT_EQ(simulator.state().player_hp, 80);
    EXPECT_EQ(simulator.state().enemy_hp, 38);
    EXPECT_EQ(simulator.state().energy, 3);
    EXPECT_EQ(simulator.state().hand_size, 5);
    EXPECT_EQ(simulator.state().hand[0], Card::defend);
    EXPECT_EQ(simulator.state().hand[2], Card::attack);
    EXPECT_THROW(simulator.snapshot(), std::invalid_argument);
    EXPECT_FALSE(simulator.unsupported_fields().empty());
}
TEST_F(ReplayParity, ExportsCppChangesAndPreservesCardIdentityAfterHandRemoval) {
    SimulatorSnapshot simulator;
    simulator.load(Snapshot::parse(states["initial"]));
    simulator.apply({{"kind", "play_card"}, {"card_instance", "card_0"}});
    simulator.apply({{"kind", "play_card"}, {"card_instance", "card_2"}, {"target", "enemy_0"}});
    const auto exported = simulator.produced_state();
    EXPECT_EQ(simulator.state().player_block, 5);
    EXPECT_EQ(simulator.state().enemy_hp, 32);
    EXPECT_EQ(exported["combat"]["players"][0]["energy"], 1);
    const auto player = states["initial"]["combat"]["players"][0]["creature"].get<std::string>();
    EXPECT_EQ(exported.at(Json::json_pointer(player))["block"], 5);
    const auto enemy = states["initial"]["combat"]["enemies"][0].get<std::string>();
    EXPECT_EQ(exported.at(Json::json_pointer(enemy))["monster"]["current_hp"], 32);
    EXPECT_EQ(exported["combat"]["players"][0]["piles"]["Hand"],
        Json::array({"/combat/cards/card_1", "/combat/cards/card_3", "/combat/cards/card_4"}));
    EXPECT_EQ(exported["combat"]["players"][0]["piles"]["Discard"],
        Json::array({"/combat/cards/card_0", "/combat/cards/card_2"}));
    EXPECT_FALSE(exported.contains("rng"));
    EXPECT_FALSE(exported["combat"].contains("history"));
}
TEST_F(ReplayParity, SimulatorBackendCannotPassWithIncompleteState) {
    SimulatorSnapshot simulator;
    backend = simulator.backend();
    try { (void)run(); FAIL() << "Partial state must not pass full-state parity"; }
    catch (const std::runtime_error& error) {
        EXPECT_NE(std::string(error.what()).find("initial: Full run-parity state export is unsupported"), std::string::npos);
    }
    EXPECT_EQ(simulator.state().player_hp, CombatState{}.player_hp);
    EXPECT_NE(simulator.state().player_hp, states["initial"]["players"][0]["current_hp"]);
    // Changing expected data never initializes or updates the simulator.
    states["initial"]["players"][0]["current_hp"] = 1;
    EXPECT_THROW(run(), std::runtime_error);
    EXPECT_EQ(simulator.state().player_hp, CombatState{}.player_hp);
}
TEST_F(ReplayParity, UnsupportedLoadAndActionsDoNotMutateAdapter) {
    SimulatorSnapshot simulator;
    EXPECT_THROW(simulator.snapshot(), std::logic_error);
    simulator.load(Snapshot::parse(states["initial"]));
    const auto before = simulator.produced_state();
    for (const auto& action : std::vector<Json>{
        {{"kind", "end_turn"}},
        {{"kind", "play_card"}, {"card_instance", "unknown"}},
        {{"kind", "play_card"}, {"card_instance", "card_5"}},
        {{"kind", "play_card"}, {"card_instance", "card_2"}},
        {{"kind", "play_card"}, {"card_instance", "card_0"}, {"target", "enemy_0"}}}) {
        EXPECT_THROW(simulator.apply(action), std::invalid_argument);
        EXPECT_EQ(before, simulator.produced_state());
    }
    auto unsupported = states["initial"];
    unsupported["combat"]["cards"]["card_0"]["saved"]["id"] = "CARD.BASH";
    EXPECT_THROW(simulator.load(Snapshot::parse(unsupported)), std::invalid_argument);
    EXPECT_EQ(before, simulator.produced_state());
}
TEST_F(ReplayParity, AdapterDoesNotCopyUnmodeledExpectedFields) {
    SimulatorSnapshot simulator;
    simulator.load(Snapshot::parse(states["initial"]));
    const auto produced = simulator.produced_state();
    EXPECT_FALSE(produced.contains("rng"));
    EXPECT_FALSE(produced.contains("native_model_state"));
    auto changed = states["initial"];
    changed["combat"]["cards"]["card_0"]["dynamic_vars"]["Block"] = 999;
    simulator.load(Snapshot::parse(changed));
    EXPECT_EQ(produced, simulator.produced_state());
    EXPECT_THROW(simulator.snapshot(), std::invalid_argument);
}

}
}
