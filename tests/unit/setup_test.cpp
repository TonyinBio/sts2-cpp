#include "sts2/state/setup.hpp"
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <fstream>

namespace sts2::engine {
namespace {
using Json = nlohmann::json;
Json names(std::span<const ContentId> ids) { Json out = Json::array(); for (auto id : ids) out.push_back(SetupRules::name(id)); return out; }
Json bag(const RelicBag& bag) {
    constexpr std::array<std::string_view, 8> tiers{"common", "uncommon", "rare", "shop", "event", "ancient", "starter", "other"};
    Json out = Json::object();
    for (auto tier : bag.order) out[std::string(tiers[static_cast<std::size_t>(tier)])] = names(bag.tiers[static_cast<std::size_t>(tier)]);
    return {{"relic_id_lists", out}};
}
TEST(SeededSetup, EntireRelicBagsRoomQueuesAndUpFrontRngMatchNativeRun) {
    std::ifstream stream(KERNEL_RUN_FIXTURE); const auto native = Json::parse(stream)["initial"];
    SetupState state; ASSERT_EQ(initialize_setup(state, {}).status, StepStatus::awaiting_input);
    EXPECT_EQ(state.numeric_seed, game_hash("ORACLECHECK01"));
    EXPECT_EQ(bag(state.shared_relics), native["shared_relic_grab_bag"]);
    EXPECT_EQ(bag(state.player_relics), native["players"][0]["relic_grab_bag"]);
    EXPECT_EQ(state.hp, native["players"][0]["current_hp"]); EXPECT_EQ(state.gold, native["players"][0]["gold"]);
    for (std::size_t i = 0; i < state.acts.size(); ++i) {
        SCOPED_TRACE("act " + std::to_string(i)); const auto& rooms = state.acts[i]; const auto& expected = native["acts"][i];
        EXPECT_EQ(SetupRules::name(rooms.act), expected["id"]);
        EXPECT_EQ(names(rooms.events), expected["rooms"]["event_ids"]);
        EXPECT_EQ(names(rooms.normal), expected["rooms"]["normal_encounter_ids"]);
        EXPECT_EQ(names(rooms.elites), expected["rooms"]["elite_encounter_ids"]);
        EXPECT_EQ(SetupRules::name(rooms.boss), expected["rooms"]["boss_id"]);
        EXPECT_EQ(SetupRules::name(rooms.ancient), expected["rooms"]["ancient_id"]);
    }
    const auto& rng = state.rng.get(RunStream::up_front).state(); const auto& expected = native["rng"]["rngs"]["up_front"];
    EXPECT_EQ(rng.counter, expected["counter"]);
    for (unsigned i = 0; i < 4; ++i) EXPECT_EQ(rng.words[i], expected["s" + std::to_string(i)]);
    constexpr std::array<const char*, 3> streams{"rewards", "shops", "transformations"};
    for (std::size_t i = 0; i < streams.size(); ++i) for (unsigned j = 0; j < 4; ++j)
        EXPECT_EQ(state.player_rng.streams[i].state().words[j], native["players"][0]["rng"]["rngs"][streams[i]]["s" + std::to_string(j)]);
}
TEST(SeededSetup, RejectsInvalidConfigurationWithoutPublishingPartialState) {
    SetupState state; ASSERT_EQ(initialize_setup(state, {}).status, StepStatus::awaiting_input); const auto before = state;
    SeededConfig config; config.character = "unknown";
    EXPECT_EQ(initialize_setup(state, config).status, StepStatus::unsupported_content); EXPECT_EQ(state, before);
    config = {}; config.acts[1] = "ACT.GLORY";
    EXPECT_EQ(initialize_setup(state, config).status, StepStatus::unsupported_content); EXPECT_EQ(state, before);
    config = {}; config.ascension = 11;
    EXPECT_EQ(initialize_setup(state, config).status, StepStatus::unsupported_content); EXPECT_EQ(state, before);
}
TEST(SeededSetup, AllCharactersAndAscensionsHaveIndependentInventoryAndRng) {
    for (const auto& character : SetupRules::characters()) for (std::uint8_t ascension = 0; ascension <= 10; ++ascension) {
        SeededConfig config; config.character = SetupRules::name(character.id); config.ascension = ascension;
        SetupState state; ASSERT_EQ(initialize_setup(state, config).status, StepStatus::awaiting_input);
        EXPECT_EQ(state.deck.size(), character.deck.size() + (ascension >= 5 ? 1 : 0));
        EXPECT_EQ(state.potion_slots, ascension >= 4 ? 2 : 3);
        EXPECT_EQ(state.acts[2].second_boss != no_content, ascension >= 10);
        if (ascension >= 10) EXPECT_NE(state.acts[2].boss, state.acts[2].second_boss);
        auto copy = state; copy.player_relics.tiers[0].clear(); EXPECT_NE(state, copy);
    }
}
}
}
