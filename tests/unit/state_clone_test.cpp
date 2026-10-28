#include "sts2/engine/action.hpp"
#include "sts2/observation/run.hpp"
#include "sts2/persistence/checkpoint.hpp"
#include "storage_allocator.hpp"
#include <gtest/gtest.h>

namespace sts2::engine {
namespace {
using test::AllocationCounter;
static_assert(std::is_trivially_copyable_v<CombatData>);
static_assert(std::is_trivially_copyable_v<RunData>);

TEST(CompleteStateClone, PublicCommonRunAndCombatClonesAllocateNothing) {
    AllocationCounter allocation;
    {
        RunState source(allocation.allocator()), run_copy(allocation.allocator());
        ASSERT_EQ(initialize_seeded_combat(source, {}).status, StepStatus::awaiting_input);
        ASSERT_TRUE(source.overflow.empty());
        ASSERT_TRUE(source.combat_overflow.empty());
        CombatState combat(ConstCombatView(*source.view().combat)), combat_copy(allocation.allocator());
        ASSERT_TRUE(combat.overflow.empty());
        const auto allocations = allocation.allocations;
        allocation.fail_after = allocations;
        EXPECT_EQ(clone_into(run_copy, source), CloneStatus::cloned);
        EXPECT_EQ(clone_into(combat_copy, combat), CloneStatus::cloned);
        EXPECT_EQ(allocation.allocations, allocations);
        EXPECT_EQ(source, run_copy); EXPECT_EQ(combat, combat_copy);
        CombatState relocated(allocation.allocator());
        std::memcpy(&relocated.data, &combat.data, sizeof(CombatData));
        EXPECT_TRUE(valid_combat(relocated)); EXPECT_EQ(relocated, combat);
        Scratch scratch;
        const CombatInput input{run_copy.view().boundary, CombatInputKind::end_turn};
        EXPECT_EQ(apply_action(run_copy, input, scratch).status, StepStatus::awaiting_input);
        EXPECT_NE(run_copy, source);
        EXPECT_EQ(combat, combat_copy);
    }
    EXPECT_EQ(allocation.outstanding, 0u);
}

TEST(CompleteStateClone, LargePublicRunBranchesAndRestoresWithIndependentOverflow) {
    Scratch transition_scratch;
    RunState source;
    SeededConfig config; config.seed = "KERNELRUN14";
    ASSERT_EQ(initialize_run(source, config).status, StepStatus::awaiting_input);
    // An authored large inventory exercises the same public entry/transition
    // boundaries; queue generation and map topology remain untouched.
    auto setup = source.view();
    while (setup.deck.size() < 200) setup.deck.push_back({SetupRules::find("CARD.STRIKE_IRONCLAD"), static_cast<std::uint32_t>(setup.deck.size()), 0, 1});
    ASSERT_TRUE(valid_run(source));
    ASSERT_EQ(apply_input(source, RunInput{source.view().boundary, RunInputKind::choose_option, 0}, transition_scratch).status, StepStatus::awaiting_input);
    ASSERT_EQ(apply_input(source, RunInput{source.view().boundary, RunInputKind::proceed}, transition_scratch).status, StepStatus::awaiting_input);
    const auto& map = *source.view().maps[0];
    MapNodeId destination = no_node;
    for (auto node : map.nodes[map.start].children.values()) if (map.nodes[node].kind == MapKind::monster) { destination = node; break; }
    ASSERT_NE(destination, no_node);
    ASSERT_EQ(apply_input(source, RunInput{source.view().boundary, RunInputKind::travel, 0, map.nodes[destination].col, map.nodes[destination].row}, transition_scratch).status, StepStatus::awaiting_input);
    ASSERT_FALSE(source.overflow.empty()); ASSERT_FALSE(source.combat_overflow.empty());
    ASSERT_EQ(source.view().combat->cards.slots().size(), 200u);
    RunState cloned, restored; Scratch scratch;
    clone_into(cloned, source);
    const auto encoded = checkpoint(source).bytes;
    ASSERT_EQ(restore(restored, encoded, scratch), RestoreStatus::restored);
    EXPECT_EQ(checkpoint(restored).bytes, encoded);
    const CombatInput input{source.view().boundary, CombatInputKind::end_turn};
    ASSERT_EQ(apply_action(source, input, scratch).status, StepStatus::awaiting_input);
    EXPECT_EQ(apply_action(cloned, input, scratch).status, StepStatus::awaiting_input);
    EXPECT_EQ(apply_action(restored, input, scratch).status, StepStatus::awaiting_input);
    EXPECT_EQ(source, cloned); EXPECT_EQ(source, restored);
    const auto unchanged = checkpoint(source).bytes;
    cloned.view().deck[199].upgrade = 1;
    cloned.view().combat->cards.get({199, cloned.view().combat_generation})->upgraded = true;
    EXPECT_EQ(checkpoint(source).bytes, unchanged);
    EXPECT_NE(source, cloned); EXPECT_EQ(source, restored);
    clone_into(cloned, source); // Reuse a destination that already owns overflow.
    EXPECT_EQ(cloned, source);
    RunState common;
    ASSERT_EQ(initialize_run(common, {}).status, StepStatus::awaiting_input);
    clone_into(cloned, common);
    EXPECT_EQ(cloned, common); EXPECT_TRUE(cloned.combat_overflow.empty());
}

TEST(CompleteStateClone, CheckpointsIgnoreStorageModeAndRetainedCapacity) {
    CombatState source; Scratch scratch;
    ASSERT_EQ(initialize_combat(source, {}, scratch).status, StepStatus::awaiting_input);
    auto other = source;
    other.view().history.reserve(256); other.view().plays.reserve(100); other.view().seed.reserve(100);
    ASSERT_FALSE(other.overflow.empty());
    EXPECT_TRUE(valid_combat(other)); EXPECT_EQ(source, other);
    EXPECT_EQ(checkpoint(source).bytes, checkpoint(other).bytes);
    source.reset();
    EXPECT_TRUE(source.overflow.empty()); EXPECT_EQ(source.data.phase, CombatPhase::uninitialized);
}

TEST(CompleteStateClone, FailedPublicInitializationAndCloneKeepDestinationUnchanged) {
    const std::array<CardSpec, 200> deck{};
    CombatConfig config; config.deck = deck;
    AllocationCounter allocation;
    {
        CombatState destination(allocation.allocator()), source;
        Scratch scratch;
        ASSERT_EQ(initialize_combat(destination, {}, scratch).status, StepStatus::awaiting_input);
        ASSERT_EQ(initialize_combat(source, config, scratch).status, StepStatus::awaiting_input);
        const auto saved = checkpoint(destination).bytes;
        allocation.fail_after = allocation.allocations;
        EXPECT_EQ(initialize_combat(destination, config, scratch).status, StepStatus::resource_limit);
        EXPECT_EQ(checkpoint(destination).bytes, saved);
        EXPECT_EQ(clone_into(destination, source), CloneStatus::resource_limit);
        EXPECT_EQ(checkpoint(destination).bytes, saved);
        EXPECT_EQ(restore(destination, checkpoint(source).bytes, scratch), RestoreStatus::resource_limit);
        EXPECT_EQ(checkpoint(destination).bytes, saved);
    }
    EXPECT_EQ(allocation.outstanding, 0u);
}

TEST(CompleteStateClone, MultiDomainCloneFailureNeverPublishesPartialOwnership) {
    RunState source;
    ASSERT_EQ(initialize_seeded_combat(source, {}).status, StepStatus::awaiting_input);
    source.view().visited.reserve(100);
    source.view().deck.reserve(100);
    source.view().combat->history.reserve(200);
    ASSERT_TRUE(valid_run(source));
    for (std::size_t allowed = 0; allowed <= 5; ++allowed) {
        AllocationCounter allocation;
        {
            RunState destination(allocation.allocator());
            ASSERT_EQ(initialize_run(destination, {}).status, StepStatus::awaiting_input);
            const auto unchanged = checkpoint(destination).bytes;
            allocation.fail_after = allocation.allocations + allowed;
            if (allowed < 5) {
                EXPECT_EQ(clone_into(destination, source), CloneStatus::resource_limit);
                EXPECT_EQ(checkpoint(destination).bytes, unchanged);
                EXPECT_TRUE(destination.overflow.empty());
                EXPECT_TRUE(destination.combat_overflow.empty());
            } else {
                EXPECT_EQ(clone_into(destination, source), CloneStatus::cloned); EXPECT_EQ(destination, source);
                destination.view().deck[0].upgrade = 1;
                EXPECT_EQ(source.view().deck[0].upgrade, 0u);
            }
        }
        EXPECT_EQ(allocation.outstanding, 0u);
    }
}

TEST(CompleteStateClone, PendingChoicePreservesReferencedDetachedSources) {
    const std::array deck{CardSpec{CardId::strike}, CardSpec{CardId::survivor}, CardSpec{CardId::defend}, CardSpec{CardId::bash}};
    CombatConfig config; config.deck = deck; config.enemy_hp = 40;
    CombatState source; Scratch scratch;
    ASSERT_EQ(initialize_combat(source, config, scratch).status, StepStatus::awaiting_input);
    const auto find = [&](CardId id) {
        for (const auto card : source.zone(Zone::hand)) if (source.view().cards.get(card)->definition == id) return card;
        return CardHandle{};
    };
    const auto strike = find(CardId::strike), survivor = find(CardId::survivor), defend = find(CardId::defend), bash = find(CardId::bash);
    ASSERT_EQ(apply_combat(source, {source.view().boundary, CombatInputKind::play_card, strike, true}, scratch).status, StepStatus::awaiting_input);
    auto discard = source.zone(Zone::discard);
    discard.erase(std::find(discard.begin(), discard.end(), strike));
    ASSERT_TRUE(valid_combat(source)); // Logical removal retains a referenced pool slot.
    ASSERT_EQ(apply_combat(source, {source.view().boundary, CombatInputKind::play_card, survivor}, scratch).status, StepStatus::awaiting_input);
    ASSERT_EQ(source.view().phase, CombatPhase::discard_choice);
    source.view().history.reserve(200);
    const auto encoded = checkpoint(source).bytes;
    CombatState cloned, restored; clone_into(cloned, source);
    ASSERT_EQ(restore(restored, encoded, scratch), RestoreStatus::restored);
    const CombatInput select{source.view().boundary, CombatInputKind::select_card, defend};
    ASSERT_EQ(apply_combat(cloned, select, scratch).status, StepStatus::awaiting_input);
    ASSERT_EQ(apply_combat(restored, select, scratch).status, StepStatus::awaiting_input);
    EXPECT_EQ(cloned, restored); EXPECT_EQ(cloned.view().rng, restored.view().rng);
    EXPECT_TRUE(cloned.view().cards.get(strike)); EXPECT_EQ(checkpoint(source).bytes, encoded);
    ASSERT_EQ(apply_combat(source, {source.view().boundary, CombatInputKind::select_card, bash}, scratch).status, StepStatus::awaiting_input);
    EXPECT_NE(source, cloned);
}

TEST(CompleteStateClone, VictoryReleasesCompletedOverflowGraphButPreservesIdentityProgress) {
    const std::array<CardSpec, 200> deck{};
    CombatConfig config; config.deck = deck; config.enemy_hp = 1;
    CombatState state; Scratch scratch;
    ASSERT_EQ(initialize_combat(state, config, scratch).status, StepStatus::awaiting_input);
    ASSERT_FALSE(state.overflow.empty());
    const auto card = state.zone(Zone::hand).front();
    ASSERT_EQ(apply_combat(state, {state.view().boundary, CombatInputKind::play_card, card, true}, scratch).status, StepStatus::combat_won);
    EXPECT_TRUE(state.overflow.empty()); EXPECT_TRUE(state.view().cards.slots().empty());
    EXPECT_EQ(state.view().cards.data.highest_generation, 1u);
    ASSERT_TRUE(valid_combat(state));
    CombatState restored; ASSERT_EQ(restore(restored, checkpoint(state).bytes, scratch), RestoreStatus::restored);
    EXPECT_EQ(restored, state); EXPECT_TRUE(restored.overflow.empty());
}

TEST(CompleteStateClone, InvalidStorageIdsCountsAndAliasingAreRejectedBeforeAccess) {
    CombatState source; Scratch scratch;
    ASSERT_EQ(initialize_combat(source, {}, scratch).status, StepStatus::awaiting_input);
    auto invalid = source;
    invalid.data.history.overflow_id = 123;
    EXPECT_FALSE(valid_combat(invalid));
    invalid = source; invalid.data.history.count = UINT32_MAX;
    EXPECT_FALSE(valid_combat(invalid));
    invalid = source; invalid.view().history.reserve(100);
    invalid.data.plays.overflow_id = invalid.data.history.overflow_id;
    EXPECT_FALSE(valid_combat(invalid));
}
} // namespace
} // namespace sts2::engine
