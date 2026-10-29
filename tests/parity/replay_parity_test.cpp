#include "sts2/parity/replay.hpp"
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
}
}
