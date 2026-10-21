#include "sts2/integration/session.hpp"
#include "sts2/persistence/checkpoint.hpp"
#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <limits>

namespace sts2::engine {
namespace {
using Json = nlohmann::json;
Json recording() {
    std::ifstream input(std::filesystem::path(KERNEL_RUN_PREFIX_FIXTURE).parent_path().parent_path() / "run-shrinker-victory/actions.json");
    return Json::parse(input);
}
Session until(std::size_t actions) {
    const auto fixture = recording(); Session session; session.initialize(fixture["setup"]);
    for (std::size_t i = 0; i < actions; ++i) {
        const auto result = session.apply(fixture["actions"][i]["action"]);
        EXPECT_TRUE(result.status == StepStatus::awaiting_input || result.status == StepStatus::combat_won);
    }
    return session;
}
TEST(RunCheckpoint, EveryLifecycleBoundaryRoundTripsAllFieldsAndNativeExport) {
    const auto fixture = recording(); Session session; session.initialize(fixture["setup"]); Scratch scratch;
    const auto check = [&] {
        ASSERT_TRUE(valid_run(session.state())); const auto encoded = checkpoint(session.state()); RunState restored;
        ASSERT_EQ(restore(restored, encoded.bytes, scratch), RestoreStatus::restored);
        EXPECT_EQ(restored, session.state()); EXPECT_EQ(checkpoint(restored).bytes, encoded.bytes);
        EXPECT_EQ(NativeRunExporter{}.snapshot(restored), session.snapshot());
    };
    check();
    for (const auto& action : fixture["actions"]) {
        SCOPED_TRACE(action["id"].get<std::string>());
        const auto result = session.apply(action["action"]);
        ASSERT_TRUE(result.status == StepStatus::awaiting_input || result.status == StepStatus::combat_won);
        check();
    }
}
TEST(RunCheckpoint, RestoredRewardSelectionBranchesIndependentlyAndKeepsRandomPositions) {
    const auto session = until(16); ASSERT_TRUE(session.state().rewards->selecting_card);
    const auto encoded = checkpoint(session.state()); RunState a, b; Scratch scratch;
    ASSERT_EQ(restore(a, encoded.bytes, scratch), RestoreStatus::restored);
    ASSERT_EQ(restore(b, encoded.bytes, scratch), RestoreStatus::restored);
    const auto random = a.setup.player_rng;
    EXPECT_EQ(apply_run(a, {a.boundary, RunInputKind::choose_reward_card, 0}).status, StepStatus::awaiting_input);
    EXPECT_EQ(apply_run(b, {b.boundary, RunInputKind::reward_alternative, 0}).status, StepStatus::awaiting_input);
    EXPECT_EQ(SetupRules::name(a.setup.deck.back().definition), "CARD.TRUE_GRIT");
    EXPECT_EQ(a.setup.deck.size(), b.setup.deck.size() + 1); EXPECT_EQ(a.setup.player_rng, random); EXPECT_EQ(b.setup.player_rng, random);
    EXPECT_EQ(checkpoint(session.state()).bytes, encoded.bytes); EXPECT_TRUE(valid_run(a)); EXPECT_TRUE(valid_run(b));
    auto sparse_ids = session.state(); sparse_ids.setup.deck[0].instance = 100;
    ASSERT_TRUE(valid_run(sparse_ids));
    EXPECT_EQ(apply_run(sparse_ids, {sparse_ids.boundary, RunInputKind::choose_reward_card, 0}).status, StepStatus::awaiting_input);
    EXPECT_EQ(sparse_ids.setup.deck.back().instance, 101u); EXPECT_TRUE(valid_run(sparse_ids));
}
TEST(RunCheckpoint, CorruptionCompatibilityAndMalformedPayloadsAreTransactional) {
    const auto session = until(16); const auto saved = checkpoint(session.state()); auto target = session.state(); Scratch scratch;
    scratch.steps = 73;
    for (const auto size : {std::size_t{0}, std::size_t{159}, saved.bytes.size() / 2, saved.bytes.size() - 1}) {
        EXPECT_NE(restore(target, std::span(saved.bytes).first(size), scratch), RestoreStatus::restored);
        EXPECT_EQ(target, session.state()); EXPECT_EQ(scratch.steps, 73u);
    }
    // Checksummed changes exercise structural validation, not just the trailer.
    for (std::size_t offset = 0; offset < saved.bytes.size() - 8; offset += 79) {
        auto changed = saved.bytes; changed[offset] ^= std::byte{0xff};
        const auto hash = game_hash({reinterpret_cast<const char*>(changed.data()), changed.size() - 8});
        for (unsigned i = 0; i < 8; ++i) changed[changed.size() - 8 + i] = std::byte(hash >> (8 * i));
        auto candidate = target; const auto status = restore(candidate, changed, scratch);
        if (status == RestoreStatus::restored) { EXPECT_TRUE(valid_run(candidate)); EXPECT_EQ(checkpoint(candidate).bytes, changed); }
        else { EXPECT_EQ(candidate, target); EXPECT_EQ(scratch.steps, 73u); }
        scratch.steps = 73;
    }
    EXPECT_EQ(restore(target, saved.bytes, scratch, {1}), RestoreStatus::resource_limit);
    EXPECT_EQ(target, session.state());
}
TEST(RunDecisions, RejectedRewardAndTravelActionsPreserveInventoryAndClock) {
    auto session = until(13); const auto before = session.state();
    for (const auto& action : std::vector<Json>{
        {{"kind", "take_reward"}, {"index", 99}}, {{"kind", "select_card_reward"}, {"card_index", 0}},
        {{"kind", "proceed"}}, {{"kind", "discard_potion"}, {"potion_index", 0}}}) {
        EXPECT_EQ(session.apply(action).status, StepStatus::invalid_action); EXPECT_EQ(session.state(), before);
    }
    auto copy = before;
    EXPECT_EQ(apply_run(copy, {{0}, RunInputKind::take_reward, 0}).reason, Restriction::stale_boundary); EXPECT_EQ(copy, before);
    ASSERT_EQ(session.apply({{"kind", "take_reward"}, {"index", 0}}).status, StepStatus::awaiting_input);
    const auto taken = session.state();
    EXPECT_EQ(session.apply({{"kind", "take_reward"}, {"index", 0}}).status, StepStatus::invalid_action); EXPECT_EQ(session.state(), taken);
    auto map = until(recording()["actions"].size()); const auto at_map = map.state();
    EXPECT_EQ(map.apply({{"kind", "select_map_node"}, {"col", 2}, {"row", 2}}).status, StepStatus::unsupported_content);
    EXPECT_EQ(map.state(), at_map);
}
TEST(RunValidation, CorruptMapEdgeMetadataAndRewardFlagsAreRejected) {
    auto session = until(16); auto state = session.state();
    state.maps[0]->nodes[state.current].children.used = UINT8_MAX; EXPECT_FALSE(valid_run(state));
    EXPECT_THROW((void)checkpoint(state), std::invalid_argument);
    state = session.state(); state.maps[0]->nodes[state.maps[0]->start].children.used = UINT8_MAX; EXPECT_FALSE(valid_run(state));
    state = session.state(); state.maps[0]->starts.free_count = UINT8_MAX; EXPECT_FALSE(valid_run(state));
    state = session.state(); state.setup.card_rarity_offset = std::numeric_limits<float>::quiet_NaN(); EXPECT_FALSE(valid_run(state));
    state = session.state(); state.rewards->selected_card = 200; EXPECT_FALSE(valid_run(state));
}
}
} // namespace sts2::engine
