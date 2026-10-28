#include "sts2/engine/action.hpp"
#include "sts2/engine/run.hpp"
#include "sts2/observation/run.hpp"
#include "sts2/integration/native_run.hpp"
#include "sts2/integration/session.hpp"
#include "sts2/persistence/checkpoint.hpp"
#include "sts2/parity/snapshot.hpp"
#include <gtest/gtest.h>
#include <fstream>
#include <filesystem>

namespace sts2::engine {
namespace {
using Json = nlohmann::json;
void compare_all(const Json& expected, const Json& actual) {
    const auto differences = sts2::parity::compare(sts2::parity::Snapshot::parse(expected), sts2::parity::Snapshot::parse(actual));
    for (std::size_t i = 0; i < std::min<std::size_t>(differences.size(), 15); ++i)
        ADD_FAILURE() << differences[i].path << " expected=" << differences[i].expected << " actual=" << differences[i].actual;
    EXPECT_TRUE(differences.empty()) << differences.size() << " full-state differences";
}
TEST(PublicRunParity, ExistingDenseAndSparseRecordingsMatchAtCorrespondingDecisions) {
    const auto root = std::filesystem::path(KERNEL_RUN_PREFIX_FIXTURE).parent_path().parent_path();
    for (const auto scenario : {"neow-map", "combat-victory", "combat-victory-dense", "combat-block-death",
            "clock-zero", "clock-step", "clock-limit", "run-shrinker-victory", "run-shrinker-death",
            "run-shrinker-victory-public", "run-shrinker-death-public", "run-shrinker-interface-skip-public"}) {
        SCOPED_TRACE(scenario);
        std::ifstream action_stream(root / scenario / "actions.json"); const auto fixture = Json::parse(action_stream);
        std::ifstream state_stream(root / scenario / "states.json"); const auto expected = Json::parse(state_stream);
        Session historical; historical.initialize(fixture["setup"]);
        auto player = historical.state(); Scratch scratch;
        std::vector<CardHandle> labels;
        const auto capture_labels = [&] {
            if (player.view().combat && labels.empty())
                for (const auto& zone : player.view().combat->zone_views()) labels.insert(labels.end(), zone.begin(), zone.end());
        };
        // Recording aliases are diagnostic identity correspondence derived from
        // independently initialized objects, never from expected checkpoints.
        capture_labels();
        std::size_t next = 0;
        compare_all(expected["initial"], NativeRunExporter{}.snapshot(player));
        for (const auto& entry : fixture["actions"]) {
            SCOPED_TRACE(entry["id"].get<std::string>());
            const auto& a = entry["action"]; const auto kind = a["kind"].get<std::string>();
            if (kind != "proceed") {
                const auto before = checkpoint(player).bytes;
                (void)observe(player); (void)get_actions(player); (void)describe(observe(player));
                EXPECT_EQ(advance_to_decision(player, scratch).boundary, player.view().boundary);
                EXPECT_EQ(checkpoint(player).bytes, before);
                PlayerInput input = RunInput{player.view().boundary, RunInputKind::skip_rewards};
                if (kind == "play_card") input = CombatInput{player.view().boundary, CombatInputKind::play_card,
                    labels.at(std::stoul(a["card_instance"].get<std::string>().substr(5))), a.contains("target")};
                else if (kind == "end_turn") input = CombatInput{player.view().boundary, CombatInputKind::end_turn};
                else {
                    RunInput run{player.view().boundary, RunInputKind::skip_rewards};
                    if (kind == "choose_option") { run.kind = RunInputKind::choose_option; run.option = a["option_index"]; }
                    else if (kind == "select_map_node") { run.kind = RunInputKind::travel; run.col = a["col"]; run.row = a["row"]; }
                    else if (kind == "take_reward") { run.kind = RunInputKind::take_reward; run.option = a["index"]; }
                    else if (kind == "select_card_reward") { run.kind = RunInputKind::choose_reward_card; run.option = a["card_index"]; }
                    else if (kind == "discard_potion") { run.kind = RunInputKind::discard_potion; run.option = a["potion_index"]; }
                    else if (kind == "skip_card_reward" || kind == "reward_alternative") { run.kind = RunInputKind::reward_alternative; run.option = a.value("index", 0u); }
                    else ASSERT_EQ(kind, "skip_rewards");
                    input = run;
                }
                const auto offered = get_actions(player);
                const auto offered_input = std::find_if(offered.begin(), offered.end(), [&](const PlayerAction& action) {
                    if (action.input.index() != input.index()) return false;
                    if (const auto* a = std::get_if<RunInput>(&input)) {
                        const auto& b = std::get<RunInput>(action.input);
                        return a->kind == b.kind && a->option == b.option && a->col == b.col && a->row == b.row;
                    }
                    const auto& a = std::get<CombatInput>(input); const auto& b = std::get<CombatInput>(action.input);
                    return a.kind == b.kind && a.card == b.card && a.target_enemy == b.target_enemy;
                });
                ASSERT_NE(offered_input, offered.end());
                input = offered_input->input;
                EXPECT_EQ(validate_action(player, input), Restriction::none);
                EXPECT_EQ(checkpoint(player).bytes, before);
                RunState restored; ASSERT_EQ(restore(restored, before, scratch), RestoreStatus::restored);
                auto cloned = player;
                const auto result = apply_action(player, input, scratch);
                ASSERT_TRUE(result.status == StepStatus::awaiting_input || result.status == StepStatus::combat_won || result.status == StepStatus::player_died);
                EXPECT_EQ(apply_action(restored, input, scratch).status, result.status);
                EXPECT_EQ(apply_action(cloned, input, scratch).status, result.status);
                EXPECT_EQ(restored, player); EXPECT_EQ(cloned, player);
                capture_labels();
            }
            (void)historical.apply(a);
            if (next < expected["transitions"].size() && expected["transitions"][next]["action_id"] == entry["id"]) {
                compare_all(expected["transitions"][next]["state"], historical.snapshot());
                if (historical.state().view().boundary == player.view().boundary)
                    compare_all(expected["transitions"][next]["state"], NativeRunExporter{}.snapshot(player));
                ++next;
            }
            if (historical.state().view().boundary == player.view().boundary) {
                EXPECT_EQ(historical.state(), player);
                compare_all(historical.snapshot(), NativeRunExporter{}.snapshot(player));
            }
        }
        EXPECT_EQ(next, expected["transitions"].size());
        EXPECT_EQ(player, historical.state());
    }
}
TEST(SeededRunParity, AllThirtyNativeStartingStatesMatchEveryField) {
    std::ifstream stream(KERNEL_SEEDED_SETUP_FIXTURE); const auto fixture = Json::parse(stream);
    NativeRunExporter exporter;
    for (const auto& test : fixture["cases"]) {
        SCOPED_TRACE(test["character"].get<std::string>() + " " + test["seed"].get<std::string>() + " A" + test["ascension"].dump());
        const auto character = test["character"].get<std::string>(), seed = test["seed"].get<std::string>();
        SeededConfig config; config.character = character; config.seed = seed; config.ascension = test["ascension"].get<std::uint8_t>();
        RunState state; ASSERT_EQ(initialize_run(state, config).status, StepStatus::awaiting_input);
        compare_all(test["expected"], exporter.snapshot(state));
    }
}
TEST(SeededRunParity, CanonicalNeowMapPrefixMatchesEveryFieldAtEveryAction) {
    Scratch transition_scratch;
    std::ifstream stream(KERNEL_RUN_PREFIX_FIXTURE); const auto states = Json::parse(stream);
    RunState state; ASSERT_EQ(initialize_run(state, {}).status, StepStatus::awaiting_input); NativeRunExporter exporter;
    compare_all(states["initial"], exporter.snapshot(state));
    ASSERT_EQ(apply_input(state, RunInput{state.view().boundary, RunInputKind::choose_option, 0}, transition_scratch).status, StepStatus::awaiting_input);
    compare_all(states["transitions"][0]["state"], exporter.snapshot(state));
    ASSERT_EQ(apply_input(state, RunInput{state.view().boundary, RunInputKind::proceed}, transition_scratch).status, StepStatus::awaiting_input);
    compare_all(states["transitions"][1]["state"], exporter.snapshot(state));
}
TEST(SeededRunParity, CanonicalCombatReproducesEveryFieldThroughVictory) {
    std::ifstream states_stream(KERNEL_COMBAT_FIXTURE); const auto states = Json::parse(states_stream);
    std::ifstream actions_stream(std::filesystem::path(KERNEL_COMBAT_FIXTURE).parent_path() / "actions.json");
    const auto actions = Json::parse(actions_stream);
    SeededConfig config; const auto seed = actions["setup"]["seed"].get<std::string>(); config.seed = seed;
    RunState state; ASSERT_EQ(initialize_seeded_combat(state, config).status, StepStatus::awaiting_input);
    NativeRunExporter exporter; Scratch scratch;
    // Native recording labels are assigned in first-export pile traversal order.
    // Derive this relation from our generated state, without reading a checkpoint.
    std::vector<CardHandle> labels;
    for (const auto& zone : state.view().combat->zone_views()) labels.insert(labels.end(), zone.begin(), zone.end());
    compare_all(states["initial"], exporter.snapshot(state));
    std::size_t next = 0;
    for (const auto& recorded : actions["actions"]) {
        SCOPED_TRACE(recorded["id"].get<std::string>());
        const auto& a = recorded["action"]; CombatInput input{state.view().boundary, CombatInputKind::end_turn};
        if (a["kind"] == "play_card") {
            const auto label = a["card_instance"].get<std::string>();
            input.kind = CombatInputKind::play_card; input.card = labels.at(std::stoul(label.substr(5)));
            input.target_enemy = a["target"] == "enemy_0";
        }
        const auto result = apply_input(state, input, scratch);
        ASSERT_TRUE(result.status == StepStatus::awaiting_input || result.status == StepStatus::combat_won);
        if (next < states["transitions"].size() && states["transitions"][next]["action_id"] == recorded["id"])
            compare_all(states["transitions"][next++]["state"], exporter.snapshot(state));
    }
    EXPECT_EQ(next, states["transitions"].size()); EXPECT_EQ(state.view().phase, RunPhase::combat_done);
}
TEST(SeededRunParity, SurvivorScenarioMatchesEntireNativeStateAcrossSuspensionAndRestore) {
    std::ifstream stream(KERNEL_SURVIVOR_FIXTURE); const auto fixture = Json::parse(stream);
    for (const auto& scenario : fixture["scenarios"]) {
        SCOPED_TRACE(scenario["deck"].dump());
        const auto seed = scenario["seed"].get<std::string>(); SeededConfig config; config.seed = seed;
        RunState state; ASSERT_EQ(initialize_run(state, config).status, StepStatus::awaiting_input);
        std::vector<CardSpec> deck; state.view().deck.clear();
        for (const auto& model : scenario["deck"]) {
            const auto name = "CARD." + model.get<std::string>();
            CardId id = CardId::count;
            for (unsigned i = 0; i < static_cast<unsigned>(CardId::count); ++i)
                if (Ruleset::pinned().card(static_cast<CardId>(i))->model_id == name) id = static_cast<CardId>(i);
            ASSERT_NE(id, CardId::count); deck.push_back({id});
            state.view().deck.push_back({SetupRules::find(name), static_cast<std::uint32_t>(deck.size() - 1), 0, -1});
        }
        // Match the documented scenario operations: set deck, enter combat (HP
        // roll consumes Niche), then explicitly override enemy HP before play.
        CombatConfig combat; combat.seed = seed; combat.deck = deck; combat.initial_rng = &state.view().rng;
        state.view().combat.emplace(); Scratch scratch;
        ASSERT_EQ(initialize_combat(*state.view().combat, combat, scratch).status, StepStatus::awaiting_input);
        state.view().combat->enemy.hp = state.view().combat->enemy.max_hp = scenario["enemy_hp"].get<int>();
        state.view().rng = state.view().combat->rng; state.view().acts[0].normal_visited = 1;
        state.view().phase = RunPhase::combat; state.view().direct_combat = true; state.view().combat_generation = 1;
        NativeRunExporter exporter; compare_all(scenario["states"][0], exporter.snapshot(state));
        const auto hand = state.view().combat->zone(Zone::hand);
        std::vector<CardHandle> labels(hand.begin(), hand.end());
        const auto card = labels.at(std::stoul(scenario["survivor"].get<std::string>().substr(5)));
        ASSERT_EQ(apply_input(state, CombatInput{state.view().boundary, CombatInputKind::play_card, card}, scratch).status, StepStatus::awaiting_input);
        compare_all(scenario["states"][1], exporter.snapshot(state));
        if (!scenario["selection"].is_null()) {
            const auto saved = checkpoint(*state.view().combat); CombatState restored;
            ASSERT_EQ(restore(restored, saved.bytes, scratch), RestoreStatus::restored);
            state.view().combat = std::move(restored);
            const auto selected = labels.at(std::stoul(scenario["selection"].get<std::string>().substr(5)));
            ASSERT_EQ(apply_input(state, CombatInput{state.view().boundary, CombatInputKind::select_card, selected}, scratch).status, StepStatus::awaiting_input);
            compare_all(scenario["states"][2], exporter.snapshot(state));
        }
    }
}
TEST(SeededRunParity, CanonicalWireRejectsMalformedInputWithoutChangingStateOrClock) {
    std::ifstream stream(std::filesystem::path(KERNEL_COMBAT_FIXTURE).parent_path() / "actions.json");
    auto setup = Json::parse(stream)["setup"]; Session session; session.initialize(setup);
    const auto before = session.state();
    for (const auto& action : std::vector<Json>{
        {{"kind", "play_card"}, {"card_instance", "card_02"}, {"target", "enemy_0"}},
        {{"kind", "play_card"}, {"card_instance", "card_2"}, {"target", "enemy_100"}},
        {{"kind", "end_turn"}, {"unexpected", true}},
        {{"kind", "choose_option"}, {"option_index", -1}},
        {{"kind", "choose_option"}, {"option_index", UINT64_MAX}}}) {
        EXPECT_THROW((void)session.apply(action), std::exception); EXPECT_EQ(session.state(), before);
    }
    EXPECT_EQ(session.apply({{"kind", "select_cards"}, {"card_instances", Json::array()}}).status, StepStatus::invalid_action);
    EXPECT_EQ(session.state(), before);
    setup["clock"]["seconds_per_action"] = 86401;
    EXPECT_THROW(session.initialize(setup), std::exception); EXPECT_EQ(session.state(), before);
}
TEST(SeededRunParity, ChangingExpectedFieldsCannotSupplyInitializationOrOutput) {
    RunState state; ASSERT_EQ(initialize_run(state, {}).status, StepStatus::awaiting_input); NativeRunExporter exporter;
    const auto actual = exporter.snapshot(state); auto changed = actual;
    changed["rng"]["rngs"]["up_front"]["s0"] = 1;
    changed["start_time"] = 1; changed["acts"][0]["rooms"]["boss_id"] = "ENCOUNTER.VANTOM_BOSS";
    EXPECT_NE(changed, exporter.snapshot(state)); EXPECT_EQ(actual, exporter.snapshot(state));
}
TEST(SeededRunParity, InvalidAndUnsupportedDecisionsLeaveAllRunStateUnchanged) {
    Scratch transition_scratch;
    RunState state; ASSERT_EQ(initialize_run(state, {}).status, StepStatus::awaiting_input); const auto before = state;
    EXPECT_EQ(apply_input(state, RunInput{{0}, RunInputKind::choose_option, 0}, transition_scratch).reason, Restriction::stale_boundary);
    EXPECT_EQ(state, before);
    EXPECT_EQ(apply_input(state, RunInput{state.view().boundary, RunInputKind::choose_option, 999}, transition_scratch).status, StepStatus::invalid_action);
    EXPECT_EQ(state, before);
    EXPECT_EQ(apply_input(state, RunInput{state.view().boundary, RunInputKind::choose_option, 1}, transition_scratch).status, StepStatus::unsupported_content);
    EXPECT_EQ(state, before);
}
}
}
