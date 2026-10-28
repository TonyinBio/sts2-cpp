#include "sts2/engine/action.hpp"
#include "sts2/json_io/session.hpp"
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
TEST(PublicRun, ConsecutiveScenarioCombatsReplaceGraphsAndRejectOldCards) {
    Scratch transition_scratch;
    auto state = until(13).state(); Scratch scratch;
    ASSERT_EQ(apply_input(state, RunInput{state.view().boundary, RunInputKind::skip_rewards}, transition_scratch).status, StepStatus::awaiting_input);
    ASSERT_EQ(apply_input(state, RunInput{state.view().boundary, RunInputKind::proceed}, transition_scratch).status, StepStatus::awaiting_input);
    EXPECT_FALSE(state.view().combat); EXPECT_TRUE(state.combat_overflow.empty());
    const CardHandle old_card{0, state.view().combat_generation};
    const auto saved = checkpoint(state).bytes;
    NativeRunExporter exporter;
    ASSERT_EQ(enter_combat_scenario(state, state.view().boundary, "ENCOUNTER.SHRINKER_BEETLE_WEAK", exporter.trace()).status, StepStatus::awaiting_input);
    EXPECT_EQ(state.view().combat_generation, 2u);
    EXPECT_EQ(state.view().deck.size(), 10u);
    EXPECT_FALSE(state.view().combat->cards.get(old_card));
    EXPECT_EQ(apply_action(state, CombatInput{state.view().boundary, CombatInputKind::play_card, old_card, true}, scratch).status, StepStatus::invalid_action);
    RunState restored;
    ASSERT_EQ(restore(restored, checkpoint(state).bytes, scratch), RestoreStatus::restored);
    EXPECT_EQ(restored, state);
    EXPECT_EQ(apply_action(restored, CombatInput{restored.view().boundary, CombatInputKind::end_turn}, scratch).status, StepStatus::awaiting_input);
    EXPECT_NE(restored, state);
    RunState before;
    ASSERT_EQ(restore(before, saved, scratch), RestoreStatus::restored);
    ASSERT_EQ(enter_combat_scenario(before, before.view().boundary, "ENCOUNTER.SHRINKER_BEETLE_WEAK").status, StepStatus::awaiting_input);
    EXPECT_EQ(before, state);
    const auto exported = exporter.snapshot(state);
    EXPECT_EQ(exported["comparison_revision"], 2);
    EXPECT_FALSE(exported.contains("map_point_history"));
    EXPECT_FALSE(exported["players"][0]["extra_fields"].contains("damage_dealt"));
    for (unsigned turn = 0; turn < 50 && state.view().phase == RunPhase::combat; ++turn) {
        const auto offered = get_actions(state);
        const auto attack = std::find_if(offered.begin(), offered.end(), [&](const auto& a) {
            const auto* input = std::get_if<CombatInput>(&a.input);
            return a.restriction == Restriction::none && input && input->kind == CombatInputKind::play_card && input->target_enemy;
        });
        if (attack != offered.end()) (void)apply_action(state, attack->input, scratch);
        else (void)apply_action(state, CombatInput{state.view().boundary, CombatInputKind::end_turn}, scratch);
    }
    ASSERT_EQ(state.view().phase, RunPhase::rewards);
    ASSERT_EQ(apply_action(state, RunInput{state.view().boundary, RunInputKind::skip_rewards}, scratch).status, StepStatus::awaiting_input);
    ASSERT_EQ(restore(restored, checkpoint(state).bytes, scratch), RestoreStatus::restored);
    EXPECT_EQ(restored, state);
    ASSERT_EQ(enter_combat_scenario(restored, restored.view().boundary, "ENCOUNTER.SHRINKER_BEETLE_WEAK").status, StepStatus::awaiting_input);
    EXPECT_EQ(restored.view().combat_generation, 3u);
    EXPECT_EQ(state.view().combat_generation, 2u);
    EXPECT_FALSE(restored.view().combat->cards.get(old_card));
    EXPECT_FALSE(state.view().combat); EXPECT_TRUE(state.combat_overflow.empty());
    // Retaining an older handle generation in an otherwise internally coherent
    // active combat must fail complete-run validation and transactional restore.
    const auto original = restored;
    CombatState aliased(ConstCombatView(*original.view().combat));
    std::vector<CardPool::Slot> slots(aliased.view().cards.slots().begin(), aliased.view().cards.slots().end());
    for (auto& slot : slots) slot.generation = 1;
    ASSERT_TRUE(aliased.view().cards.restore(std::move(slots), {}));
    for (auto zone : aliased.view().zone_views()) for (auto& card : zone) card.generation = 1;
    ASSERT_TRUE(valid_combat(aliased));
    auto bad = checkpoint(original).bytes;
    const auto inner = checkpoint(aliased).bytes;
    const std::vector<std::byte> magic(inner.begin(), inner.begin() + 8);
    const auto at = std::search(bad.begin(), bad.end(), magic.begin(), magic.end());
    ASSERT_NE(at, bad.end());
    std::copy(inner.begin(), inner.end(), at);
    const auto hash = game_hash({reinterpret_cast<const char*>(bad.data()), bad.size() - 8});
    for (unsigned i = 0; i < 8; ++i) bad[bad.size() - 8 + i] = std::byte(hash >> (8 * i));
    EXPECT_EQ(restore(restored, bad, scratch), RestoreStatus::malformed);
    EXPECT_EQ(restored, original);
}
TEST(PublicRun, RecycledGenerationsAndExhaustionRemainSafeAcrossCombats) {
    auto state = until(3).state(); Scratch scratch;
    auto combat = *state.view().combat;
    auto card = combat.cards.slots()[0].value;
    card.creation_order = combat.next_creation_order++;
    const auto old = combat.cards.create(card, 4096, 1);
    ASSERT_TRUE(combat.cards.erase(old));
    card.creation_order = combat.next_creation_order++;
    const auto recycled = combat.cards.create(card, 4096, 1);
    combat.zone(Zone::exhaust).push_back(recycled);
    ASSERT_GT(recycled.generation, old.generation);
    ASSERT_TRUE(valid_run(state));
    for (unsigned step = 0; step < 100 && state.view().phase == RunPhase::combat; ++step) {
        const auto offered = get_actions(state);
        const auto attack = std::find_if(offered.begin(), offered.end(), [](const auto& a) {
            const auto* input = std::get_if<CombatInput>(&a.input);
            return a.restriction == Restriction::none && input && input->kind == CombatInputKind::play_card && input->target_enemy;
        });
        if (attack != offered.end()) (void)apply_action(state, attack->input, scratch);
        else (void)apply_action(state, CombatInput{state.view().boundary, CombatInputKind::end_turn}, scratch);
    }
    ASSERT_EQ(state.view().phase, RunPhase::rewards);
    EXPECT_FALSE(state.view().combat); EXPECT_TRUE(state.combat_overflow.empty());
    EXPECT_EQ(state.view().combat_generation, recycled.generation);
    ASSERT_EQ(apply_action(state, RunInput{state.view().boundary, RunInputKind::skip_rewards}, scratch).status, StepStatus::awaiting_input);
    auto exhausted = state;
    exhausted.view().combat_generation = UINT32_MAX;
    ASSERT_TRUE(valid_run(exhausted));
    const auto before = checkpoint(exhausted).bytes;
    EXPECT_EQ(enter_combat_scenario(exhausted, exhausted.view().boundary, "ENCOUNTER.SHRINKER_BEETLE_WEAK").status, StepStatus::resource_limit);
    EXPECT_EQ(checkpoint(exhausted).bytes, before);
    ASSERT_EQ(enter_combat_scenario(state, state.view().boundary, "ENCOUNTER.SHRINKER_BEETLE_WEAK").status, StepStatus::awaiting_input);
    EXPECT_GT(state.view().combat_generation, recycled.generation);
    EXPECT_FALSE(state.view().combat->cards.get(old)); EXPECT_FALSE(state.view().combat->cards.get(recycled));
}
TEST(PublicRun, PermanentCardLinksSurviveDeckReordering) {
    NativeRunExporter exporter; Scratch scratch; scratch.trace = exporter.trace();
    const auto fixture = recording();
    SeededConfig config; const auto seed = fixture["setup"]["seed"].get<std::string>(); config.seed = seed;
    RunState state; ASSERT_EQ(initialize_seeded_combat(state, config, exporter.trace()).status, StepStatus::awaiting_input);
    // Declared scenario permutation of existing permanent objects; combat
    // aliases still point at those objects, not at their old deck indices.
    std::swap(state.view().deck.front(), state.view().deck.back());
    ASSERT_TRUE(valid_run(state));
    const auto native = exporter.snapshot(state);
    EXPECT_EQ(native["combat"]["cards"]["card_0"]["deck_version"], "/players/0/deck/9");
    EXPECT_EQ(native["combat"]["cards"]["card_0"]["saved"]["id"], "CARD.STRIKE_IRONCLAD");
    RunState restored;
    ASSERT_EQ(restore(restored, checkpoint(state).bytes, scratch), RestoreStatus::restored);
    EXPECT_EQ(restored, state);
    EXPECT_THROW((void)NativeRunExporter{}.snapshot(restored), std::invalid_argument);
}
TEST(PublicRun, ScenarioEntryRejectsUnsupportedAndPrematureRequestsWithoutMutation) {
    auto before_combat = until(2).state(); const auto unchanged = before_combat;
    EXPECT_EQ(enter_combat_scenario(before_combat, before_combat.view().boundary, "ENCOUNTER.SHRINKER_BEETLE_WEAK").status, StepStatus::invalid_action);
    EXPECT_EQ(before_combat, unchanged);
    auto state = until(recording()["actions"].size()).state(); const auto before = state;
    EXPECT_EQ(enter_combat_scenario(state, state.view().boundary, "ENCOUNTER.UNSUPPORTED").status, StepStatus::unsupported_content);
    EXPECT_EQ(state, before);
    // Acquired True Grit now participates in ordinary consecutive combat setup.
    EXPECT_EQ(enter_combat_scenario(state, state.view().boundary, "ENCOUNTER.SHRINKER_BEETLE_WEAK").status, StepStatus::awaiting_input);
    EXPECT_TRUE(valid_run(state));
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
    const auto offered = get_actions(state);
    ASSERT_EQ(offered.size(), 3u);
    EXPECT_EQ(offered[0].restriction, Restriction::none);
    EXPECT_EQ(offered[1].restriction, Restriction::unsupported_setup);
    EXPECT_FALSE(describe(view).empty());
    EXPECT_EQ(checkpoint(state).bytes, before);
    Scratch scratch;
    EXPECT_EQ(advance_to_decision(state, scratch).boundary, state.view().boundary);
    EXPECT_EQ(checkpoint(state).bytes, before);
    ASSERT_EQ(apply_action(state, offered[0].input, scratch).status, StepStatus::awaiting_input);
    EXPECT_EQ(state.view().phase, RunPhase::map);
    EXPECT_EQ(state.view().clock.elapsed_seconds(), 2);
    EXPECT_EQ(observe(state).gold, 249);
    EXPECT_EQ(apply_action(state, offered[0].input, scratch).reason, Restriction::stale_boundary);
}
TEST(PublicRun, RewardsRevealOnlyTheActiveOffersAndResumeIndependentAlternatives) {
    auto session = until(13); auto state = session.state(); Scratch scratch;
    const auto rewards = observe(state);
    ASSERT_EQ(rewards.rewards.size(), 3u);
    EXPECT_EQ(rewards.rewards[0].gold, 17);
    EXPECT_TRUE(rewards.reward_cards.empty());
    ASSERT_EQ(apply_action(state, RunInput{state.view().boundary, RunInputKind::take_reward, 2}, scratch).status, StepStatus::awaiting_input);
    const auto selection = observe(state);
    ASSERT_TRUE(selection.selecting_reward_card);
    ASSERT_EQ(selection.reward_cards.size(), 3u);
    EXPECT_EQ(selection.reward_cards[0].model_id, "CARD.TRUE_GRIT");
    EXPECT_EQ(selection.reward_cards[0].energy_cost, 1);
    EXPECT_TRUE(selection.can_skip_card_reward);
    EXPECT_FALSE(selection.can_skip_rewards);
    const auto saved = checkpoint(state).bytes;
    RunState restored; ASSERT_EQ(restore(restored, saved, scratch), RestoreStatus::restored);
    auto branch = state;
    ASSERT_EQ(apply_action(branch, RunInput{branch.view().boundary, RunInputKind::choose_reward_card, 0}, scratch).status, StepStatus::awaiting_input);
    ASSERT_EQ(apply_action(restored, RunInput{restored.view().boundary, RunInputKind::reward_alternative}, scratch).status, StepStatus::awaiting_input);
    EXPECT_EQ(observe(branch).deck.size(), observe(restored).deck.size() + 1);
    EXPECT_EQ(checkpoint(state).bytes, saved);
    EXPECT_TRUE(observe(restored).reward_cards.empty());
    EXPECT_EQ(restored.view().player_rng, state.view().player_rng);
}
TEST(PublicRun, HiddenQueuesAndDrawOrderDoNotAffectVisibleChoicesOrIds) {
    auto session = until(4); auto a = session.state(), b = a;
    std::reverse(b.view().combat->zone(Zone::draw).begin(), b.view().combat->zone(Zone::draw).end());
    std::reverse(b.view().acts[0].events.begin(), b.view().acts[0].events.end());
    (void)b.view().event_rng.next_bool();
    const auto av = observe(a), bv = observe(b);
    EXPECT_EQ(av.deck, bv.deck); EXPECT_EQ(av.map, bv.map);
    ASSERT_TRUE(av.combat); ASSERT_TRUE(bv.combat);
    EXPECT_EQ(av.combat->hand.data(), a.view().combat->zone(Zone::hand).data());
    EXPECT_TRUE(std::equal(av.combat->hand.begin(), av.combat->hand.end(), bv.combat->hand.begin(), bv.combat->hand.end()));
    EXPECT_EQ(av.combat->draw_count, bv.combat->draw_count);
    const auto aa = get_actions(a), ba = get_actions(b);
    ASSERT_EQ(aa.size(), ba.size());
    for (std::size_t i = 0; i < aa.size(); ++i) {
        EXPECT_EQ(aa[i].restriction, ba[i].restriction);
        const auto ai = std::get<CombatInput>(aa[i].input), bi = std::get<CombatInput>(ba[i].input);
        EXPECT_EQ(ai.card, bi.card); EXPECT_EQ(ai.kind, bi.kind); EXPECT_EQ(ai.target_enemy, bi.target_enemy);
    }
}
TEST(PublicRun, MalformedUnsupportedAndStaleActionsArePureAndFailuresInvalidate) {
    auto session = until(2); auto state = session.state(); const auto before = state; Scratch scratch;
    for (const PlayerInput input : {PlayerInput{RunInput{state.view().boundary, RunInputKind::travel, 99, 2, 1}},
            PlayerInput{RunInput{state.view().boundary, RunInputKind::choose_option, 0, 1, 0}},
            PlayerInput{RunInput{{0}, RunInputKind::travel, 0, 2, 1}},
            PlayerInput{RunInput{state.view().boundary, RunInputKind::travel, 0, 99, 99}}}) {
        EXPECT_EQ(apply_action(state, input, scratch).status, StepStatus::invalid_action);
        EXPECT_EQ(state, before);
    }
    auto map_return = until(recording()["actions"].size()).state(); const auto at_map = map_return;
    EXPECT_EQ(apply_action(map_return, RunInput{map_return.view().boundary, RunInputKind::travel, 0, 2, 2}, scratch).status, StepStatus::unsupported_content);
    EXPECT_EQ(map_return, at_map);
    auto combat = until(3).state(); scratch.step_limit = 0;
    const auto offered = get_actions(combat);
    const auto playable = std::find_if(offered.begin(), offered.end(), [](const auto& action) { return action.restriction == Restriction::none; });
    ASSERT_NE(playable, offered.end());
    EXPECT_EQ(apply_action(combat, playable->input, scratch).status, StepStatus::engine_error);
    EXPECT_EQ(combat.view().phase, RunPhase::invalid);
    EXPECT_FALSE(observe(combat).valid); EXPECT_TRUE(get_actions(combat).empty());
}
TEST(PublicRun, StarterVariantsExposeAndExecuteSourceBackedPreviews) {
    for (const bool upgraded : {false, true}) {
        RunState state; ASSERT_EQ(initialize_run(state, {}).status, StepStatus::awaiting_input);
        // Declared isolated scenario: three authored starter cards, upgrade all
        // or none, fixed 40-HP enemy. This does not import an expected checkpoint.
        const std::array deck{CardSpec{CardId::bash, upgraded}, CardSpec{CardId::defend, upgraded}, CardSpec{CardId::strike, upgraded}};
        CombatConfig config; config.deck = deck; config.enemy_hp = 40; config.initial_rng = &state.view().rng;
        state.view().combat.emplace(); Scratch scratch;
        ASSERT_EQ(initialize_combat(*state.view().combat, config, scratch).status, StepStatus::awaiting_input);
        state.view().rng = state.view().combat->rng;
        state.view().phase = RunPhase::combat; state.view().direct_combat = true; state.view().combat_generation = 1;
        const auto play = [&](std::string_view model) {
            const auto view = observe(state);
            const auto card = std::find_if(view.hand_cards.begin(), view.hand_cards.end(), [&](const auto& c) { return c.model_id == model; });
            EXPECT_NE(card, view.hand_cards.end());
            if (card == view.hand_cards.end()) return;
            EXPECT_EQ(apply_action(state, CombatInput{view.boundary, CombatInputKind::play_card, card->handle, card->enemy_target}, scratch).status, StepStatus::awaiting_input);
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
        EXPECT_EQ(apply_action(state, CombatInput{state.view().boundary, CombatInputKind::end_turn}, scratch).status, StepStatus::awaiting_input);
        EXPECT_TRUE(observe(state).combat->shrunk);
        const auto hp = observe(state).combat->enemy_hp;
        play("CARD.STRIKE_IRONCLAD"); EXPECT_EQ(observe(state).combat->enemy_hp, hp - (upgraded ? 9 : 6));
    }
}
TEST(PublicRun, InactivePilePreviewsKeepBaseDamageAfterShrink) {
    RunState state; Scratch scratch;
    SeededConfig config; config.seed = "KERNELRUN14";
    ASSERT_EQ(initialize_seeded_combat(state, config).status, StepStatus::awaiting_input);
    const auto initial = observe(state);
    const auto strike = std::find_if(initial.hand_cards.begin(), initial.hand_cards.end(), [](const auto& card) {
        return card.model_id == "CARD.STRIKE_IRONCLAD";
    });
    ASSERT_NE(strike, initial.hand_cards.end());
    ASSERT_EQ(apply_action(state, CombatInput{state.view().boundary, CombatInputKind::play_card, strike->handle, true}, scratch).status, StepStatus::awaiting_input);
    ASSERT_EQ(apply_action(state, CombatInput{state.view().boundary, CombatInputKind::end_turn}, scratch).status, StepStatus::awaiting_input);
    const auto before = checkpoint(state).bytes;
    const auto view = observe(state);
    ASSERT_TRUE(view.combat->shrunk);
    const auto discarded = std::find_if(view.discard.begin(), view.discard.end(), [](const auto& card) { return card.model_id == "CARD.STRIKE_IRONCLAD"; });
    ASSERT_NE(discarded, view.discard.end());
    // Native CardModel.UpdateDynamicVarPreview applies global hooks to Hand/Play
    // only; pile inspection keeps the card's base effect until it returns to hand.
    EXPECT_EQ(discarded->damage, 6);
    for (const auto& card : view.hand_cards) if (card.model_id == "CARD.STRIKE_IRONCLAD") EXPECT_EQ(card.damage, 4);
    EXPECT_EQ(checkpoint(state).bytes, before);
}
TEST(PublicRun, InspectablePilesExposeMetadataAndDrawInspectionHidesSequence) {
    auto state = until(4).state();
    const auto before = checkpoint(state).bytes;
    const auto view = observe(state);
    ASSERT_FALSE(view.draw.empty()); ASSERT_FALSE(view.discard.empty());
    EXPECT_TRUE(std::is_sorted(view.draw.begin(), view.draw.end(), [](const auto& a, const auto& b) { return a.model_id < b.model_id; }));
    for (const auto& card : view.draw) { EXPECT_FALSE(card.name.empty()); EXPECT_FALSE(describe(card).empty()); }
    for (const auto& card : view.discard) EXPECT_FALSE(card.model_id.empty());
    EXPECT_EQ(checkpoint(state).bytes, before);
    auto other = state;
    std::reverse(other.view().combat->zone(Zone::draw).begin(), other.view().combat->zone(Zone::draw).end());
    const auto reversed = observe(other);
    ASSERT_EQ(view.draw.size(), reversed.draw.size());
    for (std::size_t i = 0; i < view.draw.size(); ++i) EXPECT_EQ(view.draw[i].model_id, reversed.draw[i].model_id);
    const auto gift = describe(VisibleOption{0, "RELIC.GOLDEN_PEARL"});
    EXPECT_TRUE(gift.supported); EXPECT_EQ(gift.name, "Golden Pearl"); EXPECT_EQ(gift.description, "Gain 150 Gold.");
    const auto reward = describe(VisibleCard{0, "CARD.TRUE_GRIT", 0});
    EXPECT_TRUE(reward.supported); EXPECT_EQ(reward.description, "Gain 7 Block. Exhaust a random card from your hand.");
    EXPECT_FALSE(describe(VisibleOption{0, "RELIC.UNKNOWN"}).supported);
}
TEST(PublicRun, HiddenEncounterSupportIsNotDisclosedByTravelQueries) {
    auto a = until(2).state(), b = a;
    const auto different = std::find_if(b.view().acts[0].normal.begin(), b.view().acts[0].normal.end(),
        [&](auto encounter) { return encounter != b.view().acts[0].normal[0]; });
    ASSERT_NE(different, b.view().acts[0].normal.end());
    b.view().acts[0].normal[0] = *different;
    ASSERT_TRUE(valid_run(b));
    const auto aa = get_actions(a), ba = get_actions(b);
    ASSERT_FALSE(aa.empty()); ASSERT_EQ(aa.size(), ba.size());
    for (std::size_t i = 0; i < aa.size(); ++i) {
        EXPECT_EQ(aa[i].restriction, ba[i].restriction);
        EXPECT_EQ(validate_action(a, aa[i].input), validate_action(b, ba[i].input));
    }
    const auto before = b; Scratch scratch;
    EXPECT_EQ(apply_action(b, ba[0].input, scratch).status, StepStatus::unsupported_content);
    EXPECT_EQ(b, before);
}
TEST(RunCheckpoint, EveryLifecycleBoundaryRoundTripsCompleteGameplayState) {
    const auto fixture = recording(); Session session; session.initialize(fixture["setup"]); Scratch scratch;
    const auto check = [&] {
        ASSERT_TRUE(valid_run(session.state())); const auto encoded = checkpoint(session.state()); RunState restored;
        ASSERT_EQ(restore(restored, encoded.bytes, scratch), RestoreStatus::restored);
        EXPECT_EQ(restored, session.state()); EXPECT_EQ(checkpoint(restored).bytes, encoded.bytes);
        if (restored.view().combat) EXPECT_THROW((void)NativeRunExporter{}.snapshot(restored), std::invalid_argument);
        else EXPECT_EQ(NativeRunExporter{}.snapshot(restored), session.snapshot());
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
    Scratch transition_scratch;
    const auto session = until(16); ASSERT_TRUE(session.state().view().rewards->selecting_card);
    const auto encoded = checkpoint(session.state()); RunState a, b; Scratch scratch;
    ASSERT_EQ(restore(a, encoded.bytes, scratch), RestoreStatus::restored);
    ASSERT_EQ(restore(b, encoded.bytes, scratch), RestoreStatus::restored);
    const auto random = a.view().player_rng;
    EXPECT_EQ(apply_input(a, RunInput{a.view().boundary, RunInputKind::choose_reward_card, 0}, transition_scratch).status, StepStatus::awaiting_input);
    EXPECT_EQ(apply_input(b, RunInput{b.view().boundary, RunInputKind::reward_alternative, 0}, transition_scratch).status, StepStatus::awaiting_input);
    EXPECT_EQ(SetupRules::name(a.view().deck.back().definition), "CARD.TRUE_GRIT");
    EXPECT_EQ(a.view().deck.size(), b.view().deck.size() + 1); EXPECT_EQ(a.view().player_rng, random); EXPECT_EQ(b.view().player_rng, random);
    EXPECT_EQ(checkpoint(session.state()).bytes, encoded.bytes); EXPECT_TRUE(valid_run(a)); EXPECT_TRUE(valid_run(b));
    auto sparse_ids = session.state(); sparse_ids.view().deck[0].instance = 100;
    EXPECT_FALSE(sparse_ids.view().combat); // Completed graphs no longer impose stale deck links.
    ASSERT_TRUE(valid_run(sparse_ids));
    EXPECT_EQ(apply_input(sparse_ids, RunInput{sparse_ids.view().boundary, RunInputKind::choose_reward_card, 0}, transition_scratch).status, StepStatus::awaiting_input);
    EXPECT_EQ(sparse_ids.view().deck.back().instance, 101u); EXPECT_TRUE(valid_run(sparse_ids));
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
    Scratch transition_scratch;
    auto session = until(13); const auto before = session.state();
    for (const auto& action : std::vector<Json>{
        {{"kind", "take_reward"}, {"index", 99}}, {{"kind", "select_card_reward"}, {"card_index", 0}},
        {{"kind", "proceed"}}, {{"kind", "discard_potion"}, {"potion_index", 0}}}) {
        EXPECT_EQ(session.apply(action).status, StepStatus::invalid_action); EXPECT_EQ(session.state(), before);
    }
    auto copy = before;
    EXPECT_EQ(apply_input(copy, RunInput{{0}, RunInputKind::take_reward, 0}, transition_scratch).reason, Restriction::stale_boundary); EXPECT_EQ(copy, before);
    ASSERT_EQ(session.apply({{"kind", "take_reward"}, {"index", 0}}).status, StepStatus::awaiting_input);
    const auto taken = session.state();
    EXPECT_EQ(session.apply({{"kind", "take_reward"}, {"index", 0}}).status, StepStatus::invalid_action); EXPECT_EQ(session.state(), taken);
    auto map = until(recording()["actions"].size()); const auto at_map = map.state();
    EXPECT_EQ(map.apply({{"kind", "select_map_node"}, {"col", 2}, {"row", 2}}).status, StepStatus::unsupported_content);
    EXPECT_EQ(map.state(), at_map);
}
TEST(RunDecisions, ExhaustedClockDoesNotPublishRewardRandomnessOrOffers) {
    auto state = until(12).state();
    ASSERT_EQ(state.view().phase, RunPhase::combat_done);
    const auto elapsed = static_cast<std::int64_t>(state.view().boundary.value - 1);
    ASSERT_TRUE(state.view().clock.restore({RunClock::max_seconds - elapsed, 1}, elapsed));
    ASSERT_TRUE(valid_run(state));
    auto expected = state;
    expected.view().phase = RunPhase::invalid;
    Scratch scratch;
    const auto result = apply_input(state, RunInput{state.view().boundary, RunInputKind::proceed}, scratch);
    EXPECT_EQ(result.status, StepStatus::engine_error);
    EXPECT_EQ(result.reason, Restriction::clock_exhausted);
    EXPECT_EQ(state, expected);
}
TEST(RunValidation, CorruptMapEdgeMetadataAndRewardFlagsAreRejected) {
    auto session = until(16); auto state = session.state();
    state.view().maps[0]->nodes[state.view().current].children.used = UINT8_MAX; EXPECT_FALSE(valid_run(state));
    EXPECT_THROW((void)checkpoint(state), std::invalid_argument);
    state = session.state(); state.view().maps[0]->nodes[state.view().maps[0]->start].children.used = UINT8_MAX; EXPECT_FALSE(valid_run(state));
    state = session.state(); state.view().maps[0]->starts.free_count = UINT8_MAX; EXPECT_FALSE(valid_run(state));
    state = session.state(); state.view().card_rarity_offset = std::numeric_limits<float>::quiet_NaN(); EXPECT_FALSE(valid_run(state));
    state = session.state(); state.view().rewards->selected_card = 200; EXPECT_FALSE(valid_run(state));
}
}
} // namespace sts2::engine
