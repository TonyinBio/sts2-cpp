#include "sts2/engine/combat.hpp"
#include "sts2/observation/observe.hpp"
#include "sts2/persistence/checkpoint.hpp"
#include "sts2/core/sort.hpp"
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <bit>
#include <fstream>
#include <numeric>
#include <future>

namespace sts2::engine {
namespace {
using Json = nlohmann::json;
Json file(const char* path) { std::ifstream stream(path); return Json::parse(stream); }
CombatState initial(CombatConfig config = {}) {
    CombatState state; Scratch scratch;
    EXPECT_EQ(initialize_combat(state, config, scratch).status, StepStatus::awaiting_input);
    EXPECT_TRUE(valid_combat(state));
    return state;
}
CardHandle find_card(const CombatState& state, CardId id) {
    for (auto h : state.zone(Zone::hand)) if (state.view().cards.get(h)->definition == id) return h;
    return {};
}
StepResult play(CombatState& s, CardHandle card, Scratch& scratch) {
    return apply_combat(s, {s.view().boundary, CombatInputKind::play_card, card, Ruleset::pinned().card(s.view().cards.get(card)->definition)->enemy_target}, scratch);
}
TEST(GameRng, MatchesPinnedGameMixedCallsAndEntireFinalState) {
    const auto vectors = file(KERNEL_RNG_FIXTURE);
    for (const auto& sequence : vectors["sequences"]) {
        GameRng rng(sequence["seed"].get<std::uint64_t>());
        for (const auto& value : sequence["raw"]) EXPECT_EQ(rng.next_u64(), value.get<std::uint64_t>());
        for (std::size_t i = 0; i < sequence["ints"].size(); ++i) {
            std::int32_t value;
            ASSERT_TRUE(rng.next_int(i % 2 == 0 ? -123 : INT32_MIN, i % 2 == 0 ? 456 : INT32_MAX, value));
            EXPECT_EQ(value, sequence["ints"][i]);
        }
        for (const auto& value : sequence["bools"]) EXPECT_EQ(rng.next_bool(), value.get<bool>());
        for (const auto& bits : sequence["floats"]) {
            float value; ASSERT_TRUE(rng.next_float(-1.25f, 7.75f, value));
            EXPECT_EQ(std::bit_cast<std::uint32_t>(value), bits.get<std::uint32_t>());
        }
        float value; ASSERT_TRUE(rng.next_float(2, 2, value));
        EXPECT_EQ(std::bit_cast<std::uint32_t>(value), sequence["equal_float"]);
        std::vector<int> shuffled(32); std::iota(shuffled.begin(), shuffled.end(), 0); rng.shuffle<int>(shuffled);
        EXPECT_EQ(Json(shuffled), sequence["shuffled"]);
        EXPECT_EQ(rng.state().counter, sequence["state"]["counter"]);
        for (unsigned i = 0; i < 4; ++i) EXPECT_EQ(rng.state().words[i], sequence["state"]["s" + std::to_string(i)]);
    }
}
TEST(GameRng, HashesExactUtf8AndNamedStreamsMatchNativeRecording) {
    const auto vectors = file(KERNEL_RNG_FIXTURE);
    for (const auto& item : vectors["hashes"]) EXPECT_EQ(game_hash(item["text"].get<std::string>()), item["hash"]);
    const RngBank bank("ORACLECHECK01");
    const auto reference = file(KERNEL_COMBAT_FIXTURE)["initial"]["rng"]["rngs"];
    for (std::size_t i = 0; i < run_stream_names.size(); ++i) {
        const auto& expected = reference.at(std::string(run_stream_names[i]));
        if (expected["counter"] != 0) continue;
        for (unsigned word = 0; word < 4; ++word) EXPECT_EQ(bank.streams[i].state().words[word], expected["s" + std::to_string(word)]);
    }
}
TEST(GameRng, InvalidBoundsAndRestorePreserveCompleteState) {
    GameRng rng(123); const auto before = rng;
    std::int32_t value = 19; float f = 42;
    EXPECT_FALSE(rng.next_int(2, 2, value)); EXPECT_FALSE(rng.next_int(3, -1, value));
    EXPECT_FALSE(rng.next_float(3, 2, f)); EXPECT_FALSE(rng.restore({}));
    EXPECT_EQ(rng, before); EXPECT_EQ(value, 19); EXPECT_EQ(f, 42);
    auto wrapped = rng.state(); wrapped.counter = UINT32_MAX; ASSERT_TRUE(rng.restore(wrapped));
    (void)rng.next_u64(); EXPECT_EQ(rng.state().counter, 0u);
}
TEST(GameSort, EqualKeyIdentityOrderingMatchesDotnetAtPartitionBoundaries) {
    const auto vectors = file(KERNEL_RNG_FIXTURE);
    for (const auto& item : vectors["sorted"]) {
        std::vector<int> values(item["count"].get<std::size_t>()); std::iota(values.begin(), values.end(), 0);
        game_sort<int>(values, [](int a, int b) { return (a * 7) % 4 < (b * 7) % 4; });
        EXPECT_EQ(Json(values), item["identities"]);
    }
}
TEST(Storage, HandlesSurviveGrowthRejectStaleObjectsAndPreserveFreeMetadata) {
    InlineState<PoolData<int, struct TestTag, 2>> pool_owner;
    PoolView<int, TestTag, 2> pool{pool_owner.data, pool_owner.overflow}; pool.reserve(2);
    const auto first = pool.create(42, 100), other = pool.create(99, 100);
    for (int i = 0; i < 50; ++i) ASSERT_TRUE(pool.create(i, 100).present());
    ASSERT_NE(pool.get(first), nullptr); EXPECT_EQ(*pool.get(first), 42);
    ASSERT_TRUE(pool.erase(first)); EXPECT_EQ(pool.get(first), nullptr);
    const auto replacement = pool.create(7, 100);
    EXPECT_EQ(replacement.slot, first.slot); EXPECT_NE(replacement.generation, first.generation);
    EXPECT_EQ(*pool.get(other), 99); EXPECT_FALSE(pool.erase(first));
    InlineState<PoolData<int, TestTag, 2>> copy_owner; clone_into(copy_owner, pool_owner);
    PoolView<int, TestTag, 2> copy{copy_owner.data, copy_owner.overflow}; *copy.get(replacement) = -1; EXPECT_EQ(*pool.get(replacement), 7);
    EXPECT_FALSE(pool.create(0, 0).present());
}
TEST(Storage, PoolGrowthCapturesAliasedValuesAndPreservesFreeSlotOrder) {
    InlineState<PoolData<int, struct GrowthTag, 2>> storage;
    PoolView<int, GrowthTag, 2> pool{storage.data, storage.overflow};
    const auto first = pool.create(11, 100), second = pool.create(22, 100);
    const auto copied = pool.create(*pool.get(first), 100);
    ASSERT_TRUE(copied.present()); EXPECT_EQ(*pool.get(copied), 11);
    ASSERT_TRUE(pool.erase(first)); ASSERT_TRUE(pool.erase(second));
    EXPECT_EQ(pool.create(33, 100).slot, second.slot);
    EXPECT_EQ(pool.create(44, 100).slot, first.slot);
    EXPECT_FALSE(pool.get(first)); EXPECT_FALSE(pool.get(second));
    EXPECT_TRUE(pool.valid());
}
TEST(Engine, RejectsInvalidAndStaleInputWithoutChangingAnyState) {
    auto state = initial(); Scratch scratch;
    const auto before = checkpoint(state).bytes;
    for (auto input : {CombatInput{{0}, CombatInputKind::end_turn},
                       CombatInput{state.view().boundary, CombatInputKind::play_card, {999, 1}, true},
                       CombatInput{state.view().boundary, CombatInputKind::select_card, state.zone(Zone::hand)[0]}}) {
        EXPECT_EQ(apply_combat(state, input, scratch).status, StepStatus::invalid_action);
        EXPECT_EQ(checkpoint(state).bytes, before);
    }
    const auto h = find_card(state, CardId::strike); ASSERT_TRUE(h.present());
    EXPECT_EQ(apply_combat(state, {state.view().boundary, CombatInputKind::play_card, h, false}, scratch).reason, Restriction::invalid_target);
    EXPECT_EQ(checkpoint(state).bytes, before);
    EXPECT_EQ(play(state, h, scratch).status, StepStatus::awaiting_input);
    EXPECT_EQ(state.view().boundary.value, 2u); EXPECT_EQ(state.view().clock.elapsed_seconds(), 1);
    const auto after = state;
    EXPECT_EQ(advance_combat(state, scratch).boundary, after.view().boundary); EXPECT_EQ(state, after);
}
TEST(Engine, UsesRealCostsAndDamageModifiersWithoutIntermediateRounding) {
    const std::array deck{CardSpec{CardId::bash}, CardSpec{CardId::strike}, CardSpec{CardId::defend}};
    CombatConfig config; config.deck = deck; config.enemy_hp = 40;
    auto s = initial(config); Scratch scratch;
    ASSERT_EQ(play(s, find_card(s, CardId::bash), scratch).status, StepStatus::awaiting_input);
    EXPECT_EQ(s.view().enemy.hp, 32); EXPECT_EQ(s.view().enemy.vulnerable, 2); EXPECT_EQ(s.view().energy, 1);
    ASSERT_EQ(play(s, find_card(s, CardId::strike), scratch).status, StepStatus::awaiting_input);
    EXPECT_EQ(s.view().enemy.hp, 23); EXPECT_EQ(s.view().energy, 0);
    const auto before = checkpoint(s).bytes;
    EXPECT_EQ(play(s, find_card(s, CardId::defend), scratch).reason, Restriction::insufficient_energy);
    EXPECT_EQ(checkpoint(s).bytes, before);
    ASSERT_EQ(apply_combat(s, {s.view().boundary, CombatInputKind::end_turn}, scratch).status, StepStatus::awaiting_input);
    EXPECT_TRUE(s.view().player.shrink); EXPECT_EQ(s.view().enemy.vulnerable, 1);
    ASSERT_EQ(play(s, find_card(s, CardId::bash), scratch).status, StepStatus::awaiting_input);
    EXPECT_EQ(s.view().enemy.hp, 15); // floor(8 * .7 * 1.5), not floor(8*.7)*1.5
}
TEST(Engine, ShrinkerAlternatesMovesBlockExpiresAndDeathIsTerminal) {
    CombatConfig config; config.player_hp = 15;
    auto s = initial(config); Scratch scratch;
    ASSERT_EQ(apply_combat(s, {s.view().boundary, CombatInputKind::end_turn}, scratch).status, StepStatus::awaiting_input);
    EXPECT_EQ(s.view().player.hp, 15); EXPECT_EQ(s.view().move, BeetleMove::chomp);
    ASSERT_EQ(apply_combat(s, {s.view().boundary, CombatInputKind::end_turn}, scratch).status, StepStatus::awaiting_input);
    EXPECT_EQ(s.view().player.hp, 8); EXPECT_EQ(s.view().move, BeetleMove::stomp);
    ASSERT_EQ(apply_combat(s, {s.view().boundary, CombatInputKind::end_turn}, scratch).status, StepStatus::player_died);
    EXPECT_EQ(s.view().player.hp, 0); EXPECT_TRUE(valid_combat(s));
    const auto before = checkpoint(s).bytes;
    EXPECT_EQ(apply_combat(s, {s.view().boundary, CombatInputKind::end_turn}, scratch).status, StepStatus::invalid_action);
    EXPECT_EQ(checkpoint(s).bytes, before);
}
TEST(Engine, VictoryHealsBurningBloodAndClearsCombatPiles) {
    CombatConfig config; config.enemy_hp = 1; config.player_hp = 78;
    auto s = initial(config); Scratch scratch;
    EXPECT_EQ(play(s, find_card(s, CardId::strike), scratch).status, StepStatus::combat_won);
    EXPECT_EQ(s.view().player.hp, 80); EXPECT_FALSE(s.view().player.shrink);
    for (const auto& zone : s.view().zone_views()) EXPECT_TRUE(zone.empty());
    EXPECT_TRUE(valid_combat(s));
}
TEST(Engine, SurvivorSuspendsInsideEffectAndCloneRestoreResumeIndependently) {
    const std::array deck{CardSpec{CardId::survivor}, CardSpec{CardId::strike}, CardSpec{CardId::defend}};
    CombatConfig config; config.deck = deck;
    auto s = initial(config); Scratch scratch;
    const auto survivor = find_card(s, CardId::survivor);
    ASSERT_EQ(play(s, survivor, scratch).status, StepStatus::awaiting_input);
    ASSERT_EQ(s.view().phase, CombatPhase::discard_choice); EXPECT_EQ(s.view().player.block, 8);
    EXPECT_EQ(s.zone(Zone::play), std::vector<CardHandle>{survivor}); EXPECT_EQ(s.view().execution.size(), 1u);
    EXPECT_TRUE(valid_combat(s));
    const auto saved = checkpoint(s); CombatState branch, restored; clone_into(branch, s);
    ASSERT_EQ(restore(restored, saved.bytes, scratch), RestoreStatus::restored);
    EXPECT_EQ(s, branch); EXPECT_EQ(s, restored);
    const auto strike = find_card(s, CardId::strike), defend = find_card(s, CardId::defend);
    ASSERT_EQ(apply_combat(s, {s.view().boundary, CombatInputKind::select_card, strike}, scratch).status, StepStatus::awaiting_input);
    ASSERT_EQ(apply_combat(restored, {restored.view().boundary, CombatInputKind::select_card, strike}, scratch).status, StepStatus::awaiting_input);
    EXPECT_EQ(s, restored);
    ASSERT_EQ(apply_combat(branch, {branch.view().boundary, CombatInputKind::select_card, defend}, scratch).status, StepStatus::awaiting_input);
    EXPECT_NE(s, branch); EXPECT_EQ(s.view().player.block, 8); EXPECT_EQ(branch.view().player.block, 8);
    EXPECT_EQ(s.zone(Zone::discard), (std::vector<CardHandle>{strike, survivor}));
    EXPECT_EQ(branch.zone(Zone::discard), (std::vector<CardHandle>{defend, survivor}));
}
TEST(Engine, EmptySurvivorChoiceResolvesAutomatically) {
    const std::array deck{CardSpec{CardId::survivor, true}}; CombatConfig config; config.deck = deck;
    auto s = initial(config); Scratch scratch;
    EXPECT_EQ(play(s, find_card(s, CardId::survivor), scratch).status, StepStatus::awaiting_input);
    EXPECT_EQ(s.view().phase, CombatPhase::play); EXPECT_EQ(s.view().player.block, 11); EXPECT_TRUE(s.view().execution.empty());
}
TEST(Engine, SingleSurvivorCandidateDiscardsWithoutPublishingChoice) {
    const std::array deck{CardSpec{CardId::survivor}, CardSpec{CardId::strike}};
    CombatConfig config; config.deck = deck;
    auto s = initial(config); Scratch scratch;
    EXPECT_EQ(play(s, find_card(s, CardId::survivor), scratch).status, StepStatus::awaiting_input);
    EXPECT_EQ(s.view().phase, CombatPhase::play); EXPECT_TRUE(s.zone(Zone::hand).empty());
    EXPECT_EQ(s.zone(Zone::discard).size(), 2u); EXPECT_EQ(s.view().boundary.value, 2u);
}
TEST(Engine, InvalidSetupAndCapacityLeavePriorSimulationUnchanged) {
    auto s = initial(); const auto before = checkpoint(s).bytes; Scratch scratch; CombatConfig config;
    config.ascension = 1;
    EXPECT_EQ(initialize_combat(s, config, scratch).status, StepStatus::unsupported_content);
    EXPECT_EQ(checkpoint(s).bytes, before);
    EXPECT_EQ(initialize_combat(s, {}, scratch, Ruleset::pinned(), Limits{2}).status, StepStatus::resource_limit);
    EXPECT_EQ(checkpoint(s).bytes, before);
    config = {}; config.clock.epoch_seconds = -1;
    EXPECT_EQ(initialize_combat(s, config, scratch).status, StepStatus::unsupported_content);
    EXPECT_EQ(checkpoint(s).bytes, before);
}
TEST(Engine, ExecutionFailureInvalidatesUntilSuccessfulRestore) {
    auto s = initial(); Scratch scratch; const auto saved = checkpoint(s);
    scratch.step_limit = 0;
    EXPECT_EQ(play(s, find_card(s, CardId::strike), scratch).status, StepStatus::engine_error);
    EXPECT_EQ(s.view().phase, CombatPhase::invalid); EXPECT_THROW((void)checkpoint(s), std::invalid_argument);
    EXPECT_EQ(apply_combat(s, {s.view().boundary, CombatInputKind::end_turn}, scratch).reason, Restriction::invalid_state);
    ASSERT_EQ(restore(s, saved.bytes, scratch), RestoreStatus::restored); EXPECT_TRUE(valid_combat(s));
}
TEST(Observation, InspectionIsPureAndDoesNotExposeHiddenDrawSequence) {
    auto s = initial(); const auto before = checkpoint(s).bytes;
    for (unsigned i = 0; i < 20; ++i) {
        auto view = observe(s); EXPECT_EQ(view.draw_count, 5u);
        for (auto card : view.hand) EXPECT_FALSE(describe(*observe_card(s, card)).empty());
        EXPECT_FALSE(observe_card(s, s.zone(Zone::draw)[0]).has_value());
        ActionSpaceView actions(s); EXPECT_EQ(actions.size(), 6u);
        for (std::size_t j = 0; j < actions.size(); ++j) EXPECT_TRUE(actions.at(j).has_value());
        EXPECT_FALSE(actions.at(6).has_value());
    }
    EXPECT_EQ(checkpoint(s).bytes, before);
}
TEST(Checkpoint, CorruptionTruncationAndIncompatibleIdentityAreAtomic) {
    auto s = initial(); Scratch scratch; const auto saved = checkpoint(s);
    auto target = initial(); const auto before = checkpoint(target).bytes;
    for (std::size_t size = 0; size < saved.bytes.size(); ++size) {
        EXPECT_NE(restore(target, std::span(saved.bytes).first(size), scratch), RestoreStatus::restored);
        ASSERT_EQ(checkpoint(target).bytes, before);
    }
    auto bad = saved.bytes; bad[10] ^= std::byte{1};
    EXPECT_EQ(restore(target, bad, scratch), RestoreStatus::malformed);
    EXPECT_EQ(checkpoint(target).bytes, before);
    // Recompute checksum so the version incompatibility itself is exercised.
    const auto hash = game_hash({reinterpret_cast<const char*>(bad.data()), bad.size() - 8});
    for (unsigned i = 0; i < 8; ++i) bad[bad.size() - 8 + i] = std::byte(hash >> (8 * i));
    EXPECT_EQ(restore(target, bad, scratch), RestoreStatus::incompatible);
    EXPECT_EQ(checkpoint(target).bytes, before);
    EXPECT_EQ(restore(target, saved.bytes, scratch, {2}), RestoreStatus::resource_limit);
    EXPECT_EQ(checkpoint(target).bytes, before);
}
// Standalone mechanics diagnostics supplement the seeded full-state integration
// replay. Setup uses its own native Niche HP roll; no checkpoint values initialize it.
TEST(CombatMechanics, RecordedActionsMatchEveryModeledCheckpointIncludingShuffleIdentity) {
    auto state = initial(); Scratch scratch;
    const auto states = file(KERNEL_COMBAT_FIXTURE);
    const auto actions = file(KERNEL_COMBAT_ACTIONS);
    std::map<std::string, CardHandle> identities;
    for (const auto& [id, card] : states["initial"]["combat"]["cards"].items()) {
        const auto ref = card["deck_version"].get<std::string>();
        const auto slot = static_cast<std::uint32_t>(std::stoul(ref.substr(ref.rfind('/') + 1)));
        identities.emplace(id, CardHandle{slot, 1});
    }
    auto check = [&](const Json& reference) {
        const auto& combat = reference["combat"]; const auto& player = combat["players"][0];
        EXPECT_EQ(state.view().player.hp, reference["players"][0]["current_hp"]);
        EXPECT_EQ(state.view().player.block, combat["creatures"]["player_0"]["block"]);
        EXPECT_EQ(state.view().energy, player["energy"]); EXPECT_EQ(state.view().turn, player["turn_number"]);
        EXPECT_EQ(state.view().clock.config().epoch_seconds, reference["start_time"]);
        EXPECT_EQ(state.view().clock.now_seconds(), reference["save_time"]);
        EXPECT_EQ(state.view().clock.elapsed_seconds(), reference["run_time"]);
        bool shrink = false; int vulnerable = 0;
        for (const auto& power : combat["powers"]) {
            if (power["id"] == "POWER.SHRINK_POWER") shrink = true;
            if (power["id"] == "POWER.VULNERABLE_POWER") vulnerable = power["amount"];
        }
        EXPECT_EQ(state.view().player.shrink, shrink);
        if (!combat["enemies"].empty()) {
            const auto& enemy = combat["creatures"]["enemy_0"];
            EXPECT_EQ(state.view().enemy.hp, enemy["monster"]["current_hp"]);
            EXPECT_EQ(state.view().enemy.block, enemy["block"]); EXPECT_EQ(state.view().enemy.vulnerable, vulnerable);
            const std::array<std::string, 3> moves{"SHRINKER_MOVE", "CHOMP_MOVE", "STOMP_MOVE"};
            EXPECT_EQ(moves[static_cast<std::size_t>(state.view().move)], enemy["monster"]["ai"]["current_state"]);
        } else EXPECT_EQ(state.view().phase, CombatPhase::combat_won);
        const std::array<std::string, 5> names{"Hand", "Draw", "Discard", "Exhaust", "Play"};
        for (std::size_t i = 0; i < names.size(); ++i) {
            std::vector<CardHandle> expected;
            for (const auto& ref_json : player["piles"][names[i]]) {
                const auto ref = ref_json.get<std::string>();
                expected.push_back(identities.at(ref.substr(ref.rfind('/') + 1)));
            }
            EXPECT_EQ(state.view().zone_views()[i], expected) << names[i];
        }
        const auto& rng = state.view().rng.get(RunStream::shuffle).state();
        const auto& expected_rng = reference["rng"]["rngs"]["shuffle"];
        EXPECT_EQ(rng.counter, expected_rng["counter"]);
        for (unsigned i = 0; i < 4; ++i) EXPECT_EQ(rng.words[i], expected_rng["s" + std::to_string(i)]);
    };
    check(states["initial"]);
    std::size_t next_checkpoint = 0;
    for (const auto& entry : actions["actions"]) {
        const auto& action = entry["action"];
        if (action["kind"] == "end_turn") EXPECT_EQ(apply_combat(state, {state.view().boundary, CombatInputKind::end_turn}, scratch).status, StepStatus::awaiting_input);
        else {
            const auto h = identities.at(action["card_instance"].get<std::string>());
            EXPECT_NE(play(state, h, scratch).status, StepStatus::invalid_action);
        }
        if (next_checkpoint < states["transitions"].size() && states["transitions"][next_checkpoint]["action_id"] == entry["id"]) {
            SCOPED_TRACE(entry["id"].get<std::string>());
            check(states["transitions"][next_checkpoint++]["state"]);
        }
        ASSERT_TRUE(valid_combat(state));
    }
    EXPECT_EQ(next_checkpoint, states["transitions"].size());
    EXPECT_EQ(state.view().phase, CombatPhase::combat_won);
}
TEST(CombatMechanics, SurvivorBoundariesAndPileOrderingMatchLiveNativeCases) {
    const auto fixture = file(KERNEL_SURVIVOR_FIXTURE);
    for (const auto& scenario : fixture["scenarios"]) {
        std::vector<CardSpec> deck;
        for (const auto& id : scenario["deck"]) deck.push_back({id == "SURVIVOR" ? CardId::survivor : id == "STRIKE_IRONCLAD" ? CardId::strike : CardId::defend});
        CombatConfig config; config.seed = "CHOICE"; config.deck = deck; config.enemy_hp = 39;
        auto s = initial(config); Scratch scratch;
        std::map<std::string, CardHandle> ids;
        for (const auto& [id, card] : scenario["states"][0]["combat"]["cards"].items()) {
            const auto ref = card["deck_version"].get<std::string>();
            ids[id] = {static_cast<std::uint32_t>(std::stoul(ref.substr(ref.rfind('/') + 1))), 1};
        }
        auto check = [&](const Json& expected) {
            const auto& pc = expected["combat"]["players"][0];
            EXPECT_EQ(s.view().energy, pc["energy"]);
            EXPECT_EQ(s.view().player.block, expected["combat"]["creatures"]["player_0"]["block"]);
            EXPECT_EQ(s.view().clock.now_seconds(), expected["save_time"]); EXPECT_EQ(s.view().clock.elapsed_seconds(), expected["run_time"]);
            const std::array<std::string, 5> names{"Hand", "Draw", "Discard", "Exhaust", "Play"};
            for (std::size_t i = 0; i < names.size(); ++i) {
                std::vector<CardHandle> zone;
                for (const auto& value : pc["piles"][names[i]]) {
                    const auto ref = value.get<std::string>(); zone.push_back(ids.at(ref.substr(ref.rfind('/') + 1)));
                }
                EXPECT_EQ(s.view().zone_views()[i], zone) << names[i];
            }
            EXPECT_EQ(s.view().phase == CombatPhase::discard_choice, !expected["pending_choice"].is_null());
            const auto& native = expected["rng"]["rngs"]["shuffle"]; const auto& rng = s.view().rng.get(RunStream::shuffle).state();
            EXPECT_EQ(rng.counter, native["counter"]);
            for (unsigned i = 0; i < 4; ++i) EXPECT_EQ(rng.words[i], native["s" + std::to_string(i)]);
        };
        check(scenario["states"][0]);
        ASSERT_EQ(play(s, ids.at(scenario["survivor"].get<std::string>()), scratch).status, StepStatus::awaiting_input);
        check(scenario["states"][1]);
        if (!scenario["selection"].is_null()) {
            CombatState clone; clone_into(clone, s);
            const auto saved = checkpoint(s);
            CombatState resumed; ASSERT_EQ(restore(resumed, saved.bytes, scratch), RestoreStatus::restored);
            const CombatInput input{s.view().boundary, CombatInputKind::select_card, ids.at(scenario["selection"].get<std::string>())};
            for (auto* state : {&s, &clone, &resumed}) EXPECT_EQ(apply_combat(*state, input, scratch).status, StepStatus::awaiting_input);
            EXPECT_EQ(s, clone); EXPECT_EQ(s, resumed); check(scenario["states"][2]);
        }
    }
}
TEST(Engine, RandomizedActionsClonesAndCodecPreserveEveryStateField) {
    GameRng choices(83741); CombatState s = initial(); Scratch scratch;
    for (unsigned step = 0; step < 1500; ++step) {
        if (s.view().phase == CombatPhase::combat_won || s.view().phase == CombatPhase::player_died) s = initial();
        ActionSpaceView space(s);
        std::int32_t index; ASSERT_TRUE(choices.next_int(0, static_cast<int>(space.size()), index));
        auto input = space.at(static_cast<std::size_t>(index))->input;
        if (choices.next_bool()) input.expected_boundary.value += 1;
        CombatState clone; clone_into(clone, s); const auto before = checkpoint(s).bytes;
        const auto r = apply_combat(s, input, scratch);
        const auto copy_result = apply_combat(clone, input, scratch);
        EXPECT_EQ(r.status, copy_result.status); ASSERT_EQ(s, clone);
        if (r.status == StepStatus::invalid_action) EXPECT_EQ(checkpoint(s).bytes, before);
        else ASSERT_TRUE(valid_combat(s));
        CombatState decoded; ASSERT_EQ(restore(decoded, checkpoint(s).bytes, scratch), RestoreStatus::restored);
        ASSERT_EQ(decoded, s);
    }
}
TEST(Checkpoint, MutatedChecksummedPayloadsNeverPublishInvalidState) {
    const auto s = initial(); const auto original = checkpoint(s).bytes;
    GameRng random(2374); Scratch scratch;
    for (unsigned trial = 0; trial < 1000; ++trial) {
        auto mutated = original;
        std::int32_t at; ASSERT_TRUE(random.next_int(96, static_cast<int>(mutated.size() - 8), at));
        mutated[static_cast<std::size_t>(at)] ^= std::byte{0x80};
        const auto hash = game_hash({reinterpret_cast<const char*>(mutated.data()), mutated.size() - 8});
        for (unsigned i = 0; i < 8; ++i) mutated[mutated.size() - 8 + i] = std::byte(hash >> (8 * i));
        CombatState target; clone_into(target, s);
        const auto status = restore(target, mutated, scratch);
        if (status == RestoreStatus::restored) ASSERT_TRUE(valid_combat(target));
        else ASSERT_EQ(target, s);
    }
}
TEST(Engine, WorkersShareOnlyImmutableRulesAndProduceIdenticalCompleteStates) {
    const auto source = initial();
    auto run = [&source] {
        CombatState state; clone_into(state, source); Scratch scratch;
        while (state.view().phase == CombatPhase::play) {
            const auto r = apply_combat(state, {state.view().boundary, CombatInputKind::end_turn}, scratch);
            if (r.status == StepStatus::engine_error) throw std::runtime_error("worker transition failed");
        }
        return checkpoint(state).bytes;
    };
    std::array<std::future<std::vector<std::byte>>, 4> workers;
    for (auto& worker : workers) worker = std::async(std::launch::async, run);
    const auto expected = run();
    for (auto& worker : workers) EXPECT_EQ(worker.get(), expected);
}
TEST(Engine, LargeDeckUsesCheckedGrowthWithoutTruncation) {
    std::vector<CardSpec> deck(100, {CardId::defend}); CombatConfig config; config.deck = deck;
    auto s = initial(config); Scratch scratch;
    EXPECT_EQ(s.zone(Zone::draw).size(), 95u);
    for (unsigned i = 0; i < 22 && s.view().phase == CombatPhase::play; ++i) {
        const auto r = apply_combat(s, {s.view().boundary, CombatInputKind::end_turn}, scratch);
        EXPECT_NE(r.status, StepStatus::engine_error); EXPECT_TRUE(valid_combat(s));
    }
    CombatState copy; ASSERT_EQ(restore(copy, checkpoint(s).bytes, scratch), RestoreStatus::restored);
    EXPECT_EQ(s, copy);
}
} // namespace
} // namespace sts2::engine
