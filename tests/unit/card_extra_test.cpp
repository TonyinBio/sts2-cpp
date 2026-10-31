#include "sts2/engine/action.hpp"
#include "sts2/json_io/native_run.hpp"
#include "sts2/persistence/checkpoint.hpp"
#include "../../src/engine/mechanics/cards.hpp"
#include "storage_allocator.hpp"
#include <gtest/gtest.h>
#include <algorithm>
#include <array>

namespace sts2::engine {
namespace {
using gameplay::find_card_extra;
using gameplay::set_card_extra;
using test::AllocationCounter;

// Payload attachment tests storage contracts, not native Scythe gameplay support.
void initialize_cards(RunState& state, std::size_t count = 22) {
    std::array<CardSpec, 22> deck;
    deck.fill({CardId::strike});
    ASSERT_EQ(initialize_combat_scenario(state, {}, std::span(deck).first(count)).status, StepStatus::awaiting_input);
}
CardHandle combat_card(ConstCombatView state, std::size_t index) {
    return {static_cast<std::uint32_t>(index), state.cards.slots()[index].generation};
}
void add_extra(RunState& state, std::size_t persistent, std::size_t combat) {
    for (std::size_t i = 0; i < persistent; ++i)
        set_card_extra(state.view(), state.view().deck.handles()[i], TheScytheExtraData{10, static_cast<std::int32_t>(i)});
    for (std::size_t i = 0; i < combat; ++i)
        set_card_extra(*state.view().combat, combat_card(*state.view().combat, i), TheScytheExtraData{20, static_cast<std::int32_t>(i)});
}

TEST(CardExtra, RequestedInlineCapacitiesDoNotAllocateAndFailedPromotionIsAtomic) {
    AllocationCounter allocation;
    RunState state(allocation.allocator()); initialize_cards(state);
    const auto before = allocation.allocations;
    allocation.fail_after = before;
    add_extra(state, 10, 20);
    EXPECT_EQ(allocation.allocations, before);
    EXPECT_FALSE(state.data.card_extra.overflowed());
    EXPECT_FALSE(state.data.combat->card_extra.overflowed());
    ASSERT_TRUE(valid_run(state));
    const auto saved = checkpoint(state).bytes;
    EXPECT_THROW(set_card_extra(state.view(), state.view().deck.handles()[10], TheScytheExtraData{}), std::bad_alloc);
    EXPECT_THROW(set_card_extra(*state.view().combat, combat_card(*state.view().combat, 20), TheScytheExtraData{}), std::bad_alloc);
    EXPECT_EQ(checkpoint(state).bytes, saved);
    allocation.fail_after = SIZE_MAX;
    set_card_extra(state.view(), state.view().deck.handles()[10], TheScytheExtraData{});
    set_card_extra(*state.view().combat, combat_card(*state.view().combat, 20), TheScytheExtraData{});
    EXPECT_TRUE(state.data.card_extra.overflowed());
    EXPECT_TRUE(state.data.combat->card_extra.overflowed());
    EXPECT_TRUE(valid_run(state));
}
TEST(CardExtra, InlineCompleteStateClonesAllocateNothing) {
    AllocationCounter allocation;
    RunState state(allocation.allocator()), cloned(allocation.allocator());
    initialize_cards(state); add_extra(state, 10, 20);
    ASSERT_TRUE(state.overflow.empty()); ASSERT_TRUE(state.combat_overflow.empty());
    const auto before = allocation.allocations;
    allocation.fail_after = before;
    EXPECT_EQ(clone_into(cloned, state), CloneStatus::cloned);
    EXPECT_EQ(cloned, state); EXPECT_EQ(allocation.allocations, before);
}
TEST(CardExtra, OverflowCloneRestoreAndStandaloneExtractionKeepIndependentPayloads) {
    RunState state; initialize_cards(state); add_extra(state, 11, 21);
    const auto saved = checkpoint(state).bytes;
    RunState cloned = state, restored;
    Scratch scratch;
    ASSERT_EQ(restore(restored, saved, scratch), RestoreStatus::restored);
    EXPECT_EQ(cloned, state); EXPECT_EQ(restored, state);
    const auto persistent = state.view().deck.handles().front();
    const auto combat = combat_card(*state.view().combat, 0);
    std::get<TheScytheExtraData>(*find_card_extra(cloned.view(), persistent)).increased_damage = 91;
    std::get<TheScytheExtraData>(*find_card_extra(*cloned.view().combat, combat)).current_damage = 92;
    EXPECT_NE(cloned, state); EXPECT_EQ(restored, state);
    EXPECT_EQ(checkpoint(state).bytes, saved);
    CombatState detached{ConstCombatView(*state.view().combat)}, detached_restored;
    ASSERT_EQ(restore(detached_restored, checkpoint(detached).bytes, scratch), RestoreStatus::restored);
    EXPECT_EQ(detached, detached_restored);
    std::get<TheScytheExtraData>(*find_card_extra(detached.view(), combat)).current_damage = 93;
    EXPECT_NE(detached, detached_restored);
    EXPECT_EQ(checkpoint(state).bytes, saved);
}
TEST(CardExtra, UpdatingAnExistingHandleDoesNotAddAnotherEntry) {
    RunState state; initialize_cards(state, 2);
    const auto persistent = state.view().deck.handles().front();
    const auto combat = combat_card(*state.view().combat, 0);
    for (int i = 0; i < 3; ++i) {
        set_card_extra(state.view(), persistent, TheScytheExtraData{i, i});
        set_card_extra(*state.view().combat, combat, TheScytheExtraData{i, i});
    }
    EXPECT_EQ(state.view().card_extra.size(), 1u);
    EXPECT_EQ(state.view().combat->card_extra.size(), 1u);
    EXPECT_EQ(std::get<TheScytheExtraData>(*find_card_extra(ConstRunView(state.view()), persistent)).current_damage, 2);
    EXPECT_EQ(std::get<TheScytheExtraData>(*find_card_extra(ConstCombatView(*state.view().combat), combat)).current_damage, 2);
    EXPECT_THROW(set_card_extra(state.view(), PersistentCardHandle{}, TheScytheExtraData{}), std::invalid_argument);
    EXPECT_THROW(set_card_extra(*state.view().combat, CardHandle{}, TheScytheExtraData{}), std::invalid_argument);
}
TEST(CardExtra, DeckErasureRemovesPayloadBeforeItsSlotCanBeReused) {
    RunState state; ASSERT_EQ(initialize_run(state, {}).status, StepStatus::awaiting_input);
    const auto old = state.view().deck.handles().front();
    set_card_extra(state.view(), old, TheScytheExtraData{30, 4});
    ASSERT_TRUE(state.view().deck.erase(old));
    EXPECT_TRUE(state.view().card_extra.empty());
    state.view().deck.push_back({SetupRules::find("CARD.STRIKE_IRONCLAD"), 1000, 0, 1});
    const auto replacement = state.view().deck.handles().back();
    EXPECT_EQ(old.slot, replacement.slot); EXPECT_NE(old.generation, replacement.generation);
    EXPECT_EQ(find_card_extra(state.view(), old), nullptr);
    EXPECT_EQ(find_card_extra(state.view(), replacement), nullptr);
    EXPECT_TRUE(valid_run(state));
}
TEST(CardExtra, DuplicateAndStaleGenerationalKeysAreRejected) {
    RunState state; initialize_cards(state, 2); add_extra(state, 1, 1);
    auto run = state.view();
    run.card_extra.push_back(run.card_extra.front());
    EXPECT_FALSE(valid_run(state));
    run.card_extra.pop_back();
    auto combat = *state.view().combat;
    combat.card_extra.push_back(combat.card_extra.front());
    EXPECT_FALSE(valid_combat(combat));
    combat.card_extra.pop_back();
    ++combat.card_extra.front().card.generation;
    EXPECT_FALSE(valid_combat(combat)); EXPECT_FALSE(valid_run(state));
    EXPECT_THROW(checkpoint(state), std::invalid_argument);
    EXPECT_EQ(find_card_extra(combat, combat.card_extra.front().card), nullptr);
    --combat.card_extra.front().card.generation;
    ++state.view().card_extra.front().card.generation;
    EXPECT_FALSE(valid_run(state));
}
TEST(CardExtra, CombatCopiesPersistentPayloadAndClearsOnlyCombatStateAtVictory) {
    RunState state; Scratch scratch;
    ASSERT_EQ(initialize_combat_scenario(state, {}, {}).status, StepStatus::awaiting_input);
    const auto persistent = state.view().deck.handles().front();
    set_card_extra(state.view(), persistent, TheScytheExtraData{30, 4});
    auto first_combat = *state.view().combat;
    first_combat.enemy.hp = 1;
    CardHandle first_strike;
    for (auto card : first_combat.zone(Zone::hand))
        if (first_combat.cards.get(card)->definition == CardId::strike) { first_strike = card; break; }
    ASSERT_TRUE(first_strike.present());
    ASSERT_EQ(apply_action(state, CombatInput{state.data.boundary, CombatInputKind::play_card, first_strike, true}, scratch).status, StepStatus::awaiting_input);
    ASSERT_EQ(state.view().phase, RunPhase::rewards);
    ASSERT_EQ(apply_action(state, RunInput{state.data.boundary, RunInputKind::skip_rewards}, scratch).status, StepStatus::awaiting_input);
    ASSERT_EQ(enter_combat_scenario(state, state.data.boundary, "ENCOUNTER.SHRINKER_BEETLE_WEAK").status, StepStatus::awaiting_input);
    auto combat = *state.view().combat;
    ASSERT_EQ(combat.card_extra.size(), 1u);
    const auto card = combat.card_extra.front().card;
    ASSERT_EQ(combat.cards.get(card)->origin, persistent);
    EXPECT_EQ(*find_card_extra(combat, card), *find_card_extra(state.view(), persistent));
    std::get<TheScytheExtraData>(*find_card_extra(combat, card)).current_damage = 99;
    EXPECT_EQ(std::get<TheScytheExtraData>(*find_card_extra(state.view(), persistent)).current_damage, 30);
    combat.enemy.hp = 1;
    CardHandle strike;
    for (auto hand_card : combat.zone(Zone::hand))
        if (combat.cards.get(hand_card)->definition == CardId::strike) { strike = hand_card; break; }
    ASSERT_TRUE(strike.present());
    ASSERT_EQ(apply_action(state, CombatInput{state.data.boundary, CombatInputKind::play_card, strike, true}, scratch).status, StepStatus::awaiting_input);
    EXPECT_EQ(state.view().phase, RunPhase::rewards);
    EXPECT_FALSE(state.view().combat);
    EXPECT_TRUE(state.combat_overflow.empty());
    EXPECT_EQ(std::get<TheScytheExtraData>(*find_card_extra(state.view(), persistent)).current_damage, 30);
    EXPECT_TRUE(valid_run(state));
}
TEST(CardExtra, UnknownPayloadTagsAreRejectedWithoutChangingDestination) {
    RunState state; initialize_cards(state, 2);
    const TheScytheExtraData marker{0x12345678, 0x23456701};
    set_card_extra(state.view(), state.view().deck.handles().front(), marker);
    set_card_extra(*state.view().combat, combat_card(*state.view().combat, 0), marker);
    const auto corrupt = [](std::vector<std::byte> bytes) {
        constexpr std::array signature{std::byte{0x78}, std::byte{0x56}, std::byte{0x34}, std::byte{0x12},
            std::byte{0x01}, std::byte{0x67}, std::byte{0x45}, std::byte{0x23}};
        const auto at = std::search(bytes.begin(), bytes.end(), signature.begin(), signature.end());
        if (at == bytes.end() || at == bytes.begin()) throw std::logic_error("Missing test payload");
        *(at - 1) = std::byte{255};
        const auto hash = game_hash({reinterpret_cast<const char*>(bytes.data()), bytes.size() - 8});
        for (unsigned i = 0; i < 8; ++i) bytes[bytes.size() - 8 + i] = std::byte(hash >> (8 * i));
        return bytes;
    };
    Scratch scratch;
    const auto before = checkpoint(state).bytes;
    EXPECT_EQ(restore(state, corrupt(before), scratch), RestoreStatus::malformed);
    EXPECT_EQ(checkpoint(state).bytes, before);
    CombatState combat{ConstCombatView(*state.view().combat)};
    const auto before_combat = checkpoint(combat).bytes;
    EXPECT_EQ(restore(combat, corrupt(before_combat), scratch), RestoreStatus::malformed);
    EXPECT_EQ(checkpoint(combat).bytes, before_combat);
}
TEST(CardExtra, NativeDiagnosticsRejectUnmappedSideData) {
    RunState state; ASSERT_EQ(initialize_run(state, {}).status, StepStatus::awaiting_input);
    set_card_extra(state.view(), state.view().deck.handles().front(), TheScytheExtraData{30, 4});
    NativeRunExporter exporter;
    EXPECT_THROW(exporter.snapshot(state), std::invalid_argument);
}
}
}
