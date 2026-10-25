#include "sts2/integration/session.hpp"
#include "sts2/observation/run.hpp"
#include "sts2/persistence/checkpoint.hpp"
#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <limits>
#include <algorithm>

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
TEST(PublicRun, StartingChoicesAreVisiblePureAndForcedProgressIsAutomatic) {
    RunState state;
    ASSERT_EQ(initialize_run(state, {}).status, StepStatus::awaiting_input);
    const auto before = checkpoint(state).bytes;
    const auto view = observe(state);
    ASSERT_EQ(view.options.size(), 3u);
    EXPECT_EQ(view.options[0].model_id, "RELIC.GOLDEN_PEARL");
    EXPECT_EQ(view.hp, 80);
    EXPECT_EQ(view.deck.size(), 10u);
    const auto offered = actions(state);
    ASSERT_EQ(offered.size(), 3u);
    EXPECT_EQ(offered[0].restriction, Restriction::none);
    EXPECT_EQ(offered[1].restriction, Restriction::unsupported_setup);
    EXPECT_FALSE(describe(view).empty());
    EXPECT_EQ(checkpoint(state).bytes, before);
    Scratch scratch;
    EXPECT_EQ(advance_player(state, scratch).boundary, state.boundary);
    EXPECT_EQ(checkpoint(state).bytes, before);
    ASSERT_EQ(apply_player(state, offered[0].input, scratch).status, StepStatus::awaiting_input);
    EXPECT_EQ(state.phase, RunPhase::map);
    EXPECT_EQ(state.clock.elapsed_seconds(), 2);
    EXPECT_EQ(observe(state).gold, 249);
    EXPECT_EQ(apply_player(state, offered[0].input, scratch).reason, Restriction::stale_boundary);
}
TEST(PublicRun, RewardsRevealOnlyTheActiveOffersAndResumeIndependentAlternatives) {
    auto session = until(13); auto state = session.state(); Scratch scratch;
    const auto rewards = observe(state);
    ASSERT_EQ(rewards.rewards.size(), 3u);
    EXPECT_EQ(rewards.rewards[0].gold, state.rewards->gold);
    EXPECT_TRUE(rewards.reward_cards.empty());
    ASSERT_EQ(apply_player(state, RunInput{state.boundary, RunInputKind::take_reward, 2}, scratch).status, StepStatus::awaiting_input);
    const auto selection = observe(state);
    ASSERT_TRUE(selection.selecting_reward_card);
    ASSERT_EQ(selection.reward_cards.size(), 3u);
    EXPECT_EQ(selection.reward_cards[0].model_id, "CARD.TRUE_GRIT");
    EXPECT_TRUE(selection.can_skip_card_reward);
    EXPECT_FALSE(selection.can_skip_rewards);
    const auto saved = checkpoint(state).bytes;
    RunState restored; ASSERT_EQ(restore(restored, saved, scratch), RestoreStatus::restored);
    auto branch = state;
    ASSERT_EQ(apply_player(branch, RunInput{branch.boundary, RunInputKind::choose_reward_card, 0}, scratch).status, StepStatus::awaiting_input);
    ASSERT_EQ(apply_player(restored, RunInput{restored.boundary, RunInputKind::reward_alternative}, scratch).status, StepStatus::awaiting_input);
    EXPECT_EQ(observe(branch).deck.size(), observe(restored).deck.size() + 1);
    EXPECT_EQ(checkpoint(state).bytes, saved);
    EXPECT_TRUE(observe(restored).reward_cards.empty());
    EXPECT_EQ(restored.setup.player_rng, state.setup.player_rng);
}
TEST(PublicRun, HiddenQueuesAndDrawOrderDoNotAffectVisibleChoicesOrIds) {
    auto session = until(4); auto a = session.state(), b = a;
    std::reverse(b.combat->zone(Zone::draw).begin(), b.combat->zone(Zone::draw).end());
    std::reverse(b.setup.acts[0].events.begin(), b.setup.acts[0].events.end());
    (void)b.event_rng.next_bool();
    const auto av = observe(a), bv = observe(b);
    EXPECT_EQ(av.deck, bv.deck); EXPECT_EQ(av.map, bv.map);
    ASSERT_TRUE(av.combat); ASSERT_TRUE(bv.combat);
    EXPECT_EQ(av.combat->hand.data(), a.combat->zone(Zone::hand).data());
    EXPECT_TRUE(std::equal(av.combat->hand.begin(), av.combat->hand.end(), bv.combat->hand.begin(), bv.combat->hand.end()));
    EXPECT_EQ(av.combat->draw_count, bv.combat->draw_count);
    const auto aa = actions(a), ba = actions(b);
    ASSERT_EQ(aa.size(), ba.size());
    for (std::size_t i = 0; i < aa.size(); ++i) {
        EXPECT_EQ(aa[i].restriction, ba[i].restriction);
        const auto ai = std::get<Input>(aa[i].input), bi = std::get<Input>(ba[i].input);
        EXPECT_EQ(ai.card, bi.card); EXPECT_EQ(ai.kind, bi.kind); EXPECT_EQ(ai.target_enemy, bi.target_enemy);
    }
}
TEST(PublicRun, MalformedUnsupportedAndStaleActionsArePureAndFailuresInvalidate) {
    auto session = until(2); auto state = session.state(); const auto before = state; Scratch scratch;
    for (const PlayerInput input : {PlayerInput{RunInput{state.boundary, RunInputKind::travel, 99, 2, 1}},
            PlayerInput{RunInput{state.boundary, RunInputKind::choose_option, 0, 1, 0}},
            PlayerInput{RunInput{{0}, RunInputKind::travel, 0, 2, 1}},
            PlayerInput{RunInput{state.boundary, RunInputKind::travel, 0, 99, 99}}}) {
        EXPECT_EQ(apply_player(state, input, scratch).status, StepStatus::invalid_action);
        EXPECT_EQ(state, before);
    }
    auto map_return = until(recording()["actions"].size()).state(); const auto at_map = map_return;
    EXPECT_EQ(apply_player(map_return, RunInput{map_return.boundary, RunInputKind::travel, 0, 2, 2}, scratch).status, StepStatus::unsupported_content);
    EXPECT_EQ(map_return, at_map);
    auto combat = until(3).state(); scratch.step_limit = 0;
    const auto offered = actions(combat);
    const auto playable = std::find_if(offered.begin(), offered.end(), [](const auto& action) { return action.restriction == Restriction::none; });
    ASSERT_NE(playable, offered.end());
    EXPECT_EQ(apply_player(combat, playable->input, scratch).status, StepStatus::engine_error);
    EXPECT_EQ(combat.phase, RunPhase::invalid);
    EXPECT_FALSE(observe(combat).valid); EXPECT_TRUE(actions(combat).empty());
}
TEST(PublicRun, StarterVariantsExposeAndExecuteSourceBackedPreviews) {
    for (const bool upgraded : {false, true}) {
        RunState state; ASSERT_EQ(initialize_run(state, {}).status, StepStatus::awaiting_input);
        // Declared isolated scenario: three authored starter cards, upgrade all
        // or none, fixed 40-HP enemy. This does not import an expected checkpoint.
        const std::array deck{CardSpec{CardId::bash, upgraded}, CardSpec{CardId::defend, upgraded}, CardSpec{CardId::strike, upgraded}};
        CombatConfig config; config.deck = deck; config.enemy_hp = 40; config.initial_rng = &state.setup.rng;
        state.combat.emplace(); Scratch scratch;
        ASSERT_EQ(Simulator{}.initialize(*state.combat, config, scratch).status, StepStatus::awaiting_input);
        state.setup.rng = state.combat->rng;
        state.phase = RunPhase::combat; state.direct_combat = true;
        const auto play = [&](std::string_view model) {
            const auto view = observe(state);
            const auto card = std::find_if(view.hand_cards.begin(), view.hand_cards.end(), [&](const auto& c) { return c.model_id == model; });
            EXPECT_NE(card, view.hand_cards.end());
            if (card == view.hand_cards.end()) return;
            EXPECT_EQ(apply_player(state, Input{view.boundary, InputKind::play_card, card->handle, card->enemy_target}, scratch).status, StepStatus::awaiting_input);
        };
        const auto before = checkpoint(state).bytes;
        for (const auto& card : observe(state).hand_cards) {
            EXPECT_EQ(card.upgraded, upgraded);
            if (card.model_id == "CARD.BASH") { EXPECT_EQ(card.damage, upgraded ? 10 : 8); EXPECT_EQ(card.vulnerable, upgraded ? 3 : 2); }
            if (card.model_id == "CARD.DEFEND_IRONCLAD") EXPECT_EQ(card.block, upgraded ? 8 : 5);
            if (card.model_id == "CARD.STRIKE_IRONCLAD") EXPECT_EQ(card.damage, upgraded ? 9 : 6);
        }
        EXPECT_EQ(checkpoint(state).bytes, before);
        play("CARD.DEFEND_IRONCLAD"); EXPECT_EQ(observe(state).combat->block, upgraded ? 8 : 5);
        play("CARD.BASH"); EXPECT_EQ(observe(state).combat->enemy_hp, upgraded ? 30 : 32);
        EXPECT_EQ(observe(state).combat->enemy_vulnerable, upgraded ? 3 : 2);
        EXPECT_EQ(apply_player(state, Input{state.boundary, InputKind::end_turn}, scratch).status, StepStatus::awaiting_input);
        EXPECT_TRUE(observe(state).combat->shrunk);
        const auto hp = observe(state).combat->enemy_hp;
        play("CARD.STRIKE_IRONCLAD"); EXPECT_EQ(observe(state).combat->enemy_hp, hp - (upgraded ? 9 : 6));
    }
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
