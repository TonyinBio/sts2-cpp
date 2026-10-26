#include "sts2/parity/snapshot.hpp"
#include <gtest/gtest.h>
#include <fstream>

namespace sts2::parity {
namespace {
using Json = nlohmann::json;
Json example() { std::ifstream input(PARITY_EXAMPLE_PATH); return Json::parse(input); }
Snapshot parse(const Json& value) { return Snapshot::parse(value); }
TEST(RunParityFormat, AcceptsExampleAndMatchesIdenticalState) {
    const auto value = parse(example());
    EXPECT_TRUE(compare(value, value).empty());
}
TEST(RunParityFormat, RevisedGameplaySurfaceRemovesOnlyDeclaredDiagnosticFields) {
    const auto historical = parse(example());
    const auto revised = historical.gameplay_projection();
    EXPECT_EQ(revised.json()["comparison_revision"], 2);
    EXPECT_FALSE(revised.json().contains("map_point_history"));
    EXPECT_TRUE(compare(historical, revised).empty());
    auto changed = example();
    changed["players"][0]["extra_fields"]["damage_dealt"] = 999;
    EXPECT_FALSE(compare(historical, parse(changed)).empty()); // Historical v1/v1 stays exact.
    EXPECT_TRUE(compare(revised, parse(changed)).empty());
    changed["players"][0]["gold"] = 999;
    const auto differences = compare(revised, parse(changed));
    ASSERT_EQ(differences.size(), 1u);
    EXPECT_EQ(differences[0].path, "/players/0/gold");
    auto malformed = revised.json(); malformed["map_point_history"] = nullptr;
    EXPECT_THROW(parse(malformed), std::invalid_argument);
    malformed = revised.json(); malformed["players"][0]["extra_fields"]["damage_dealt"] = nullptr;
    EXPECT_THROW(parse(malformed), std::invalid_argument);
    malformed = revised.json(); malformed["rng"]["rngs"].erase("up_front");
    EXPECT_FALSE(compare(revised, parse(malformed)).empty());
}
TEST(RunParityFormat, RevisedProjectionPreservesExactWireDecimalTokens) {
    auto wire = example().dump();
    const std::string key = "\"unknown_map_point_monster_odds_value\":0.1";
    const auto position = wire.find(key);
    ASSERT_NE(position, std::string::npos);
    wire.replace(position, key.size(), "\"unknown_map_point_monster_odds_value\":0.10000000000000000000000001");
    const auto exact = Snapshot::parse_text(wire);
    const auto revised = exact.gameplay_projection();
    EXPECT_EQ(revised.numbers().at("/odds/unknown_map_point_monster_odds_value"), exact.numbers().at("/odds/unknown_map_point_monster_odds_value"));
    EXPECT_TRUE(compare(exact, revised).empty());
    EXPECT_FALSE(compare(parse(example()), revised).empty());
}
TEST(RunParityFormat, PreservesRemovedMonsterWithNullAIThroughHistory) {
    auto value = example();
    value["combat"]["creatures"]["dead_slime"] = {
        {"player", nullptr}, {"side", "Enemy"}, {"block", 0}, {"hp_is_infinite", false},
        {"slot_name", nullptr}, {"pet_owner", nullptr}, {"powers", Json::array()},
        {"monster", {{"id", "MONSTER.TWIG_SLIME_S"}, {"current_hp", 0}, {"max_hp", 10},
            {"spawned_this_turn", false}, {"is_performing_move", false}, {"ai", nullptr},
            {"rng", value["rng"]["rngs"]["monster_ai"]}, {"model_fields", Json::object()}}}};
    value["combat"]["history"][0]["actor"] = "/combat/creatures/dead_slime";
    EXPECT_NO_THROW(parse(value));
    EXPECT_TRUE(compare(parse(value), parse(value)).empty());
    value["combat"]["creatures"]["dead_slime"]["monster"]["ai"] = Json::object();
    EXPECT_THROW(parse(value), std::invalid_argument);
}
TEST(RunParityFormat, RetainsDetachedDeckOriginalAndRejectsInvalidDeckAliases) {
    auto value = example();
    auto original = value["combat"]["cards"]["discard_candidate"];
    original["deck_version"] = nullptr;
    original["has_been_removed_from_state"] = true;
    value["combat"]["cards"]["stolen_original"] = original;
    value["combat"]["cards"]["discard_candidate"]["deck_version"] = "/combat/cards/stolen_original";
    value["players"][0]["deck"].erase(0);
    EXPECT_NO_THROW(parse(value));
    // Returned originals may be registered again before their reward is collected.
    auto returned = value;
    returned["combat"]["cards"]["stolen_original"]["has_been_removed_from_state"] = false;
    EXPECT_NO_THROW(parse(returned));
    EXPECT_FALSE(compare(parse(value), parse(returned)).empty());
    for (const auto& field : {"owner", "deck_version"}) {
        auto invalid = value;
        invalid["combat"]["cards"]["stolen_original"][field] =
            field == std::string("owner") ? Json(nullptr) : Json("/combat/cards/discard_candidate");
        EXPECT_THROW(parse(invalid), std::invalid_argument);
    }
    value["combat"]["players"][0]["piles"]["Hand"].push_back("/combat/cards/stolen_original");
    EXPECT_THROW(parse(value), std::invalid_argument);
}
TEST(RunParityFormat, ComparesEntireStateWithoutFieldExclusions) {
    const auto expected = example();
    for (const auto& pointer : {
        "/save_time", "/run_time", "/start_time", "/players/0/gold",
        "/rng/rngs/shuffle/counter", "/combat/history/0/round_number",
        "/combat/cards/survivor/energy_cost/base"}) {
        auto actual = expected;
        const auto path = Json::json_pointer(pointer);
        ASSERT_TRUE(actual.contains(path)) << pointer;
        actual[path] = actual[path].get<std::int64_t>() + 1;
        const auto differences = compare(parse(expected), parse(actual));
        ASSERT_EQ(differences.size(), 1u) << pointer;
        EXPECT_EQ(differences[0].path, pointer);
    }
}
TEST(RunParityFormat, ReportsExactNativeFieldAndValues) {
    auto value = example();
    const auto hp = value["players"][0]["current_hp"].get<int>();
    value["players"][0]["current_hp"] = hp - 1;
    const auto diff = compare(parse(example()), parse(value));
    ASSERT_EQ(diff.size(), 1u);
    EXPECT_EQ(diff[0].path, "/players/0/current_hp");
    EXPECT_EQ(diff[0].expected, hp);
    EXPECT_EQ(diff[0].actual, hp - 1);
}
TEST(RunParityFormat, PreservesPileOrderAndAliasing) {
    auto value = example();
    auto& piles = value["combat"]["players"][0]["piles"];
    piles["Play"] = Json::array();
    piles["Hand"].push_back("/combat/cards/survivor");
    auto actual = value;
    std::swap(actual["combat"]["players"][0]["piles"]["Hand"][0], actual["combat"]["players"][0]["piles"]["Hand"][1]);
    EXPECT_FALSE(compare(parse(value), parse(actual)).empty());
}
TEST(RunParityFormat, ComparesNativeUint64WithoutPrecisionLoss) {
    auto value = example();
    const auto word = value["rng"]["rngs"]["shuffle"]["s3"].get<std::uint64_t>();
    value["rng"]["rngs"]["shuffle"]["s3"] = word ^ 1ULL;
    const auto diff = compare(parse(example()), parse(value));
    ASSERT_EQ(diff.size(), 1u);
    EXPECT_EQ(diff[0].path, "/rng/rngs/shuffle/s3");
}
TEST(RunParityFormat, IntegerBoundsHandleSignedAndUnsignedValuesExactly) {
    auto value = example();
    value["players"][0]["net_id"] = std::int64_t{1};
    EXPECT_NO_THROW(parse(value));
    value["players"][0]["net_id"] = std::uint64_t{UINT64_MAX};
    EXPECT_NO_THROW(parse(value));
    value["players"][0]["net_id"] = std::int64_t{-1};
    EXPECT_THROW(parse(value), std::invalid_argument);
}
TEST(RunParityFormat, SupportsNonCombatAndCompletedRun) {
    auto value = example();
    value["combat"] = nullptr; value["pending_choice"] = nullptr; value["current_room"] = nullptr;
    EXPECT_NO_THROW(parse(value));
    value["boundary_kind"] = "run_complete"; value["run_ending_reason"] = "victory";
    EXPECT_NO_THROW(parse(value));
    value["run_ending_reason"] = nullptr;
    EXPECT_THROW(parse(value), std::invalid_argument);
}
TEST(RunParityFormat, CombatCompletionIsNotRunCompletion) {
    auto value = example();
    value["pending_choice"] = nullptr;
    value["combat"]["turn_control"]["is_in_progress"] = false;
    EXPECT_NO_THROW(parse(value));
}
TEST(RunParityFormat, PreservesActiveEventCombatRoomFlagsAndParent) {
    auto value = example();
    auto& room = value["current_room"];
    room["encounter_id"] = "ENCOUNTER.MYSTERIOUS_KNIGHT_EVENT_ENCOUNTER";
    room["parent_event_id"] = "EVENT.THE_LANTERN_KEY";
    room["is_pre_finished"] = false;
    room["should_resume_parent_event"] = false;
    EXPECT_NO_THROW(parse(value));
    auto changed = value;
    changed["current_room"]["is_pre_finished"] = true;
    EXPECT_FALSE(compare(parse(value), parse(changed)).empty());
    changed = value;
    changed["current_room"]["parent_event_id"] = nullptr;
    EXPECT_FALSE(compare(parse(value), parse(changed)).empty());
}
TEST(RunParityFormat, PreservesNativeDefaultsAndMissingModelState) {
    auto value = example();
    value["combat"]["cards"]["survivor"]["saved"]["id"] = "CARD.CLAW";
    auto actual = value;
    actual["combat"]["cards"]["survivor"]["model_fields"]["Claw._extraDamageFromClawPlays"] = 0;
    EXPECT_FALSE(compare(parse(value), parse(actual)).empty());
}
class InvalidSnapshot : public testing::TestWithParam<std::pair<std::string, Json>> {};
TEST_P(InvalidSnapshot, RejectsUnsupportedOrIncompleteState) {
    auto value = example();
    const auto& [pointer, replacement] = GetParam();
    value[Json::json_pointer(pointer)] = replacement;
    EXPECT_THROW(parse(value), std::invalid_argument) << pointer;
}
INSTANTIATE_TEST_SUITE_P(Schema, InvalidSnapshot, testing::Values(
    std::pair<std::string, Json>{"/schema_version", 1},
    std::pair<std::string, Json>{"/profile", "combat-v1"},
    std::pair<std::string, Json>{"/boundary_kind", "combat_end"},
    std::pair<std::string, Json>{"/run_ending_reason", "victory"},
    std::pair<std::string, Json>{"/combat", nullptr},
    std::pair<std::string, Json>{"/current_room", nullptr},
    std::pair<std::string, Json>{"/combat/players/0/energy", 2.5},
    std::pair<std::string, Json>{"/combat/creatures/player/hp_is_infinite", 0},
    std::pair<std::string, Json>{"/combat/creatures/player/monster", Json::object()},
    std::pair<std::string, Json>{"/combat/cards/survivor/saved/id", "invalid"},
    std::pair<std::string, Json>{"/combat/cards/survivor/model_fields", {{"unverified_counter", 1}}},
    std::pair<std::string, Json>{"/combat/cards/survivor/model_fields", {{"Claw._extraDamageFromClawPlays", "three"}}},
    std::pair<std::string, Json>{"/combat/players/0/piles/Hand", Json::array({"/combat/cards/missing"})},
    std::pair<std::string, Json>{"/combat/players/0/piles/Hand", Json::array({"/combat/cards/survivor"})},
    std::pair<std::string, Json>{"/combat/players/0/piles/Hand", Json::array({"/combat/creatures/player"})},
    std::pair<std::string, Json>{"/combat/cards/discard_candidate/deck_version", "/combat/cards/survivor"},
    std::pair<std::string, Json>{"/pending_choice/min_select", 2},
    std::pair<std::string, Json>{"/pending_choice/candidates", Json::array({"/combat/creatures/player"})},
    std::pair<std::string, Json>{"/native_model_state", {{"/players/0/deck/100", {{"model_fields", Json::object()}}}}}
));
TEST(RunParityFormat, MissingFieldsDoNotReceiveDefaults) {
    auto value = example(); value["players"][0].erase("current_hp");
    EXPECT_THROW(parse(value), std::invalid_argument);
    value = example(); value.erase("native_model_state");
    EXPECT_THROW(parse(value), std::invalid_argument);
}
TEST(RunParityFormat, ComparesDecimalTokensWithoutRounding) {
    auto value = example();
    value["combat"]["cards"]["survivor"]["dynamic_vars"]["Block"] = "DECIMAL_TOKEN";
    auto text = value.dump();
    const auto at = text.find("\"DECIMAL_TOKEN\"");
    ASSERT_NE(at, std::string::npos);
    auto first = text, second = text;
    first.replace(at, 15, "0.1234567890123456789012345678");
    second.replace(at, 15, "0.1234567890123456789012345679");
    const auto diff = compare(Snapshot::parse_text(first), Snapshot::parse_text(second));
    ASSERT_EQ(diff.size(), 1u);
    EXPECT_EQ(diff[0].path, "/combat/cards/survivor/dynamic_vars/Block");
    second = text;
    second.replace(at, 15, "1234567890123456789012345678e-28");
    EXPECT_TRUE(compare(Snapshot::parse_text(first), Snapshot::parse_text(second)).empty());
}
TEST(RunParityFormat, RejectsDuplicateWireKeys) {
    EXPECT_THROW(Snapshot::parse_text("{\"boundary_kind\":null,\"boundary_kind\":null}"), std::invalid_argument);
}
TEST(RunParityFormat, IgnoresBijectiveLocalLabelChanges) {
    auto text = example().dump();
    const std::string old = "survivor", replacement = "renamed_card";
    std::size_t offset = 0;
    while ((offset = text.find(old, offset)) != std::string::npos) {
        text.replace(offset, old.size(), replacement); offset += replacement.size();
    }
    EXPECT_TRUE(compare(parse(example()), Snapshot::parse_text(text)).empty());
}
TEST(RunParityFormat, NormalizesSetsWhilePreservingLists) {
    auto left = example();
    left["combat"]["cards"]["survivor"]["local_keywords"] = {"Retain", "Exhaust"};
    auto right = left;
    right["combat"]["cards"]["survivor"]["local_keywords"] = {"Exhaust", "Retain"};
    EXPECT_TRUE(compare(parse(left), parse(right)).empty());
}

TEST(RunParityFormat, NativeStringsAreNotObjectReferences) {
    auto expected = example();
    expected["rng"]["seed"] = "/combat/cards/survivor";
    auto text = expected.dump();
    const std::string old = "survivor", replacement = "renamed_card";
    std::size_t offset = 0;
    while ((offset = text.find(old, offset)) != std::string::npos) {
        text.replace(offset, old.size(), replacement); offset += replacement.size();
    }
    // Local labels changed bijectively, but the native seed is real data.
    const auto diff = compare(parse(expected), Snapshot::parse_text(text));
    ASSERT_EQ(diff.size(), 1u);
    EXPECT_EQ(diff[0].path, "/rng/seed");
}
TEST(RunParityFormat, ExtraTableObjectsCannotDisappearDuringAlignment) {
    auto actual = example();
    actual["combat"]["cards"]["extra"] = actual["combat"]["cards"]["survivor"];
    EXPECT_FALSE(compare(parse(example()), parse(actual)).empty());
}

TEST(RunParityFormat, NormalizesReferenceDictionaryOrderAndLocalLabels) {
    auto expected = example();
    expected["pending_choice"] = nullptr;
    expected["combat"]["active_card_plays"] = Json::array();
    expected["combat"]["card_plays"] = Json::object();
    expected["combat"]["history"] = Json::array();
    expected["combat"]["powers"]["dampen"] = {
        {"id", "POWER.DAMPEN_POWER"}, {"amount", 1}, {"amount_on_turn_start", 0},
        {"skip_next_duration_tick", false}, {"applier", nullptr}, {"target", nullptr},
        {"dynamic_vars", Json::object()}, {"model_fields", {
            {"DampenPower.downgradedCardsToOldUpgradeLevels", Json::array({
                {{"key", "/combat/cards/survivor"}, {"value", 1}},
                {{"key", "/combat/cards/discard_candidate"}, {"value", 2}}})}}}};
    expected["combat"]["creatures"]["player"]["powers"] = {"/combat/powers/dampen"};
    auto text = expected.dump();
    for (const auto& [old, replacement] : std::vector<std::pair<std::string, std::string>>{
        {"survivor", "a"}, {"discard_candidate", "z"}}) {
        std::size_t offset = 0;
        while ((offset = text.find(old, offset)) != std::string::npos) {
            text.replace(offset, old.size(), replacement); offset += replacement.size();
        }
    }
    auto actual = Json::parse(text);
    auto& entries = actual["combat"]["powers"]["dampen"]["model_fields"]["DampenPower.downgradedCardsToOldUpgradeLevels"];
    std::swap(entries[0], entries[1]);
    EXPECT_TRUE(compare(parse(expected), parse(actual)).empty());
}

TEST(RunParityFormat, ValidatesNoncombatSelectionBoundsAndDeckAliases) {
    auto value = example();
    value["combat"] = nullptr;
    value["pending_choice"] = nullptr;
    value["native_model_state"] = Json::object();
    value["run_flow"] = {
        {"room", {{"kind", "rest_site"}, {"options", Json::array()}}},
        {"pending_choice", {{"kind", "card_select"},
            {"cards", Json::array({value["players"][0]["deck"][0]})},
            {"deck_indices", Json::array({0})}, {"min_select", 1}, {"max_select", 1},
            {"require_manual_confirmation", true}, {"cancelable", true}}},
        {"reward_sets", Json::array()},
        {"operations", Json::array({{{"kind", "choose_option"}, {"index", 1}}})},
        {"combat_rewards_offered", false}};
    EXPECT_NO_THROW(parse(value));
    for (const auto& [pointer, replacement] : std::vector<std::pair<std::string, Json>>{
        {"/run_flow/pending_choice/max_select", 2},
        {"/run_flow/pending_choice/deck_indices", Json::array()},
        {"/run_flow/pending_choice/deck_indices/0", 999},
        {"/run_flow/operations", Json::array()}}) {
        auto broken = value;
        broken[Json::json_pointer(pointer)] = replacement;
        EXPECT_THROW(parse(broken), std::invalid_argument) << pointer;
    }
}

TEST(RunParityFormat, ValidatesOrbQueueOwnershipAndIdentity) {
    auto value = example();
    auto& player = value["combat"]["players"][0];
    value["combat"]["orbs"]["dark"] = {{"id", "ORB.DARK_ORB"}, {"owner", player["player"]},
        {"has_been_removed_from_state", false}, {"model_fields", {{"DarkOrb._evokeVal", 6}}}};
    player["orbs"] = Json::array({"/combat/orbs/dark"});
    EXPECT_NO_THROW(parse(value));
    auto wrong = value;
    wrong["combat"]["orbs"]["dark"]["owner"] = player["creature"];
    EXPECT_THROW(parse(wrong), std::invalid_argument);
    player["orbs"].push_back("/combat/orbs/dark");
    EXPECT_THROW(parse(value), std::invalid_argument);
}

}
}
