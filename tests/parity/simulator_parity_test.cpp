#include "simulator_adapter.hpp"
#include "sha256.hpp"
#include <gtest/gtest.h>
#include <fstream>
#include <iterator>

namespace sts2::parity {
namespace {
using Json = nlohmann::json;
Json read(const std::string& path) { std::ifstream input(std::string(REPO_ROOT) + "/" + path); return Json::parse(input); }
TEST(RecordingDigest, StandardSha256Vectors) {
    EXPECT_EQ(sha256(""), "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    EXPECT_EQ(sha256("abc"), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    EXPECT_EQ(sha256("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"),
        "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}
class ReferenceCombat : public testing::TestWithParam<std::pair<std::string, std::string>> {};
TEST_P(ReferenceCombat, EveryTransitionMatchesWithoutResynchronization) {
    const auto& [action_path, state_path] = GetParam();
    const auto states = read(state_path);
    std::ifstream raw(std::string(REPO_ROOT) + "/" + action_path, std::ios::binary);
    const std::string bytes{std::istreambuf_iterator<char>(raw), std::istreambuf_iterator<char>()};
    ASSERT_EQ(sha256(bytes), states["actions_sha256"]);
    EXPECT_NO_THROW(replay_and_compare(Json::parse(bytes), states));
}
INSTANTIATE_TEST_SUITE_P(RecordedOracle, ReferenceCombat, testing::Values(
    std::pair<std::string, std::string>{"tests/parity/examples/starter.actions.json", "tests/parity/fixtures/starter.states.json"},
    std::pair<std::string, std::string>{"tests/parity/fixtures/ironclad-shrinker-victory.actions.json", "tests/parity/fixtures/ironclad-shrinker-victory.states.json"},
    std::pair<std::string, std::string>{"tests/parity/fixtures/ironclad-shrinker-defeat.actions.json", "tests/parity/fixtures/ironclad-shrinker-defeat.states.json"},
    std::pair<std::string, std::string>{"tests/parity/fixtures/ironclad-shrinker-random.actions.json", "tests/parity/fixtures/ironclad-shrinker-random.states.json"}));
TEST(SimulatorParity, ImportRejectsStateItCannotRepresent) {
    auto initial = read("tests/parity/fixtures/starter.states.json")["initial"];
    initial["cards"][0]["counters"]["unknown"] = 1;
    EXPECT_THROW(import_initial(Snapshot::parse(initial, simulator_features)), std::invalid_argument);
    initial = read("tests/parity/fixtures/starter.states.json")["transitions"][2]["state"];
    EXPECT_THROW(import_initial(Snapshot::parse(initial, simulator_features)), std::invalid_argument);
}
TEST(SimulatorParity, ReportsFirstActionAndFieldWithoutResynchronizing) {
    auto reference = read("tests/parity/fixtures/starter.states.json");
    reference["transitions"][1]["state"]["enemies"][0]["hp"] = 31;
    try {
        replay_and_compare(read("tests/parity/examples/starter.actions.json"), reference);
        FAIL() << "Expected mismatch";
    } catch (const std::exception& error) {
        const std::string message = error.what();
        EXPECT_NE(message.find("strike-1"), std::string::npos);
        EXPECT_NE(message.find("/enemies/0/hp"), std::string::npos);
    }
}
}
}
