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
        std::memcpy(&relocated.player, &combat.player, sizeof(CreatureState));
        std::memcpy(&relocated.enemy, &combat.enemy, sizeof(CreatureState));
        relocated.boundary = combat.boundary; relocated.clock = combat.clock; relocated.rng = combat.rng; relocated.joss_paper_counter = combat.joss_paper_counter;
        EXPECT_TRUE(valid_combat(relocated)); EXPECT_EQ(relocated, combat);
        Scratch scratch;
        const CombatInput input{run_copy.view().boundary, CombatInputKind::end_turn};
        EXPECT_EQ(apply_action(run_copy, input, scratch).status, StepStatus::awaiting_input);
        EXPECT_NE(run_copy, source);
        EXPECT_EQ(combat, combat_copy);
    }
    EXPECT_EQ(allocation.outstanding, 0u);
}

TEST(CompleteStateClone, FixedPowerSlotsPreserveApplicationOrderAndCloneWithoutAllocating) {
    AllocationCounter allocation;
    CombatState source(allocation.allocator()), branch(allocation.allocator()); Scratch scratch;
    ASSERT_EQ(initialize_combat(source, {}, scratch).status, StepStatus::awaiting_input);
    auto& player = source.view().player;
    // Apply in reverse enum order: dispatch must not become enum-ordered.
    player.power(PowerId::vulnerable) = {0, PowerId::vulnerable, 2, 1, false, true, true};
    player.power(PowerId::shrink) = {1, PowerId::shrink, -1, -1, false, true, true};
    player.power_order = {PowerId::vulnerable, PowerId::shrink}; player.power_count = 2;
    source.view().next_power_instance = 2;
    ASSERT_TRUE(valid_combat(source));
    const auto allocations = allocation.allocations;
    allocation.fail_after = allocations;
    ASSERT_EQ(clone_into(branch, source), CloneStatus::cloned);
    EXPECT_EQ(allocation.allocations, allocations);
    EXPECT_TRUE(branch.overflow.empty());
    EXPECT_EQ(branch.view().player.power_order[0], PowerId::vulnerable);
    branch.view().player.power(PowerId::vulnerable).amount = 3;
    EXPECT_EQ(player.power_amount(PowerId::vulnerable), 2);
    EXPECT_EQ(branch.view().player.power_amount(PowerId::vulnerable), 3);

    CombatState restored;
    ASSERT_EQ(restore(restored, checkpoint(source).bytes, scratch), RestoreStatus::restored);
    EXPECT_EQ(restored, source);
    EXPECT_EQ(restored.view().player.power_order[0], PowerId::vulnerable);
}

TEST(CompleteStateClone, InvalidPowerSlotIdentityMembershipAndOrderAreRejected) {
    CombatState source; Scratch scratch;
    ASSERT_EQ(initialize_combat(source, {}, scratch).status, StepStatus::awaiting_input);
    auto invalid = source;
    invalid.view().player.power_count = CreatureState::power_type_count + 1;
    EXPECT_FALSE(valid_combat(invalid));
    const auto unchanged = invalid;
    EXPECT_EQ(apply_combat(invalid, {invalid.view().boundary, CombatInputKind::end_turn}, scratch).reason, Restriction::invalid_state);
    EXPECT_EQ(invalid, unchanged);
    invalid = source;
    invalid.view().player.power_order[0] = PowerId::count;
    invalid.view().player.power_count = 1;
    EXPECT_FALSE(valid_combat(invalid));
    invalid = source;
    invalid.view().player.power(PowerId::shrink) = {0, PowerId::shrink, -1, 0, false, true, true};
    invalid.view().next_power_instance = 1;
    // A live slot without application-order membership must not be accepted.
    EXPECT_FALSE(valid_combat(invalid));
    invalid.view().player.power_count = 2;
    invalid.view().player.power_order = {PowerId::shrink, PowerId::shrink};
    EXPECT_FALSE(valid_combat(invalid));
    invalid.view().player.power_count = 1;
    invalid.view().player.power(PowerId::shrink).enemy_owner = true;
    EXPECT_FALSE(valid_combat(invalid));
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
    other.view().execution.reserve(256); other.view().seed.reserve(100);
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
    source.view().combat->execution.reserve(200);
    ASSERT_TRUE(valid_run(source));
    for (std::size_t allowed = 0; allowed <= 7; ++allowed) {
        AllocationCounter allocation;
        {
            RunState destination(allocation.allocator());
            ASSERT_EQ(initialize_run(destination, {}).status, StepStatus::awaiting_input);
            const auto unchanged = checkpoint(destination).bytes;
            allocation.fail_after = allocation.allocations + allowed;
            if (allowed < 7) {
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

TEST(CompleteStateClone, PendingChoicePreservesSourceAfterUnreferencedCardRemoval) {
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
    ASSERT_TRUE(source.view().cards.erase(strike));
    ASSERT_TRUE(valid_combat(source)); // Export-only history no longer roots this slot.
    ASSERT_EQ(apply_combat(source, {source.view().boundary, CombatInputKind::play_card, survivor}, scratch).status, StepStatus::awaiting_input);
    ASSERT_EQ(source.view().phase, CombatPhase::discard_choice);
    source.view().execution.reserve(200);
    const auto encoded = checkpoint(source).bytes;
    CombatState cloned, restored; clone_into(cloned, source);
    ASSERT_EQ(restore(restored, encoded, scratch), RestoreStatus::restored);
    const CombatInput select{source.view().boundary, CombatInputKind::select_card, defend};
    ASSERT_EQ(apply_combat(cloned, select, scratch).status, StepStatus::awaiting_input);
    ASSERT_EQ(apply_combat(restored, select, scratch).status, StepStatus::awaiting_input);
    EXPECT_EQ(cloned, restored); EXPECT_EQ(cloned.view().rng, restored.view().rng);
    EXPECT_FALSE(cloned.view().cards.get(strike)); EXPECT_EQ(checkpoint(source).bytes, encoded);
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

TEST(CompleteStateClone, SuspendedCountersAndCapturedOperandsSurviveIndependentFutures) {
    const std::array deck{CardSpec{CardId::survivor}, CardSpec{CardId::strike}, CardSpec{CardId::defend}};
    CombatConfig config; config.deck = deck; config.enemy_hp = 40;
    CombatState source; Scratch scratch;
    ASSERT_EQ(initialize_combat(source, config, scratch).status, StepStatus::awaiting_input);
    const auto find = [&](CardId id) {
        for (auto card : source.zone(Zone::hand)) if (source.view().cards.get(card)->definition == id) return card;
        return CardHandle{};
    };
    const auto survivor = find(CardId::survivor), strike = find(CardId::strike), defend = find(CardId::defend);
    ASSERT_EQ(apply_combat(source, {source.view().boundary, CombatInputKind::play_card, survivor}, scratch).status, StepStatus::awaiting_input);
    EXPECT_EQ(source.view().counters.started_plays, 1u);
    EXPECT_EQ(source.view().counters.finished_plays, 0u);
    ASSERT_EQ(source.view().execution.size(), 1u);
    EXPECT_EQ(source.view().execution.back().energy, 1);
    EXPECT_FALSE(source.view().execution.back().enemy_target);
    const auto saved = checkpoint(source).bytes;
    auto branch = source; CombatState restored;
    ASSERT_EQ(restore(restored, saved, scratch), RestoreStatus::restored);
    for (auto* state : {&branch, &restored}) {
        ASSERT_EQ(apply_combat(*state, {state->view().boundary, CombatInputKind::select_card, defend}, scratch).status, StepStatus::awaiting_input);
        EXPECT_TRUE(state->view().execution.empty());
        EXPECT_EQ(state->view().counters.finished_plays, 1u);
        EXPECT_EQ(state->view().cards.get(survivor)->last_finished_player_turn, 1);
        ASSERT_EQ(apply_combat(*state, {state->view().boundary, CombatInputKind::play_card, strike, true}, scratch).status, StepStatus::awaiting_input);
        EXPECT_EQ(state->view().enemy.hp, 34);
        EXPECT_EQ(state->view().enemy.positive_unblocked_hits, 1u);
        EXPECT_EQ(state->view().counters.started_this_turn, 2u);
        EXPECT_EQ(state->view().counters.attacks_this_turn, 1u);
        ASSERT_EQ(apply_combat(*state, {state->view().boundary, CombatInputKind::end_turn}, scratch).status, StepStatus::awaiting_input);
        EXPECT_EQ(state->view().counters.started_plays, 2u);
        EXPECT_EQ(state->view().counters.finished_plays, 2u);
        EXPECT_EQ(state->view().counters.started_this_turn, 0u);
        EXPECT_EQ(state->view().counters.attacks_this_turn, 0u);
        EXPECT_EQ(state->view().cards.get(strike)->last_finished_player_turn, 1);
    }
    EXPECT_EQ(branch, restored);
    EXPECT_EQ(checkpoint(source).bytes, saved);
    ASSERT_EQ(apply_combat(source, {source.view().boundary, CombatInputKind::select_card, strike}, scratch).status, StepStatus::awaiting_input);
    EXPECT_NE(source, branch);
}

TEST(CompleteStateClone, LongCombatDoesNotAccumulateDiagnosticStorage) {
    const std::array deck{CardSpec{CardId::defend, true}, CardSpec{CardId::defend, true}, CardSpec{CardId::defend, true}};
    CombatConfig config; config.deck = deck;
    AllocationCounter allocation; CombatState state(allocation.allocator()); Scratch scratch;
    ASSERT_EQ(initialize_combat(state, config, scratch).status, StepStatus::awaiting_input);
    const auto allocations = allocation.allocations;
    allocation.fail_after = allocations;
    const auto bytes = state.retained_bytes();
    for (unsigned turn = 0; turn < 100; ++turn) {
        while (!state.zone(Zone::hand).empty()) {
            const auto card = state.zone(Zone::hand).front();
            ASSERT_EQ(apply_combat(state, {state.view().boundary, CombatInputKind::play_card, card}, scratch).status, StepStatus::awaiting_input);
        }
        ASSERT_EQ(apply_combat(state, {state.view().boundary, CombatInputKind::end_turn}, scratch).status, StepStatus::awaiting_input);
        ASSERT_TRUE(valid_combat(state));
        EXPECT_EQ(state.retained_bytes(), bytes);
        EXPECT_TRUE(state.overflow.empty());
        EXPECT_EQ(state.view().player.power_count + state.view().enemy.power_count, 1u);
    }
    EXPECT_EQ(state.view().counters.finished_plays, 300u);
    EXPECT_EQ(allocation.allocations, allocations);
}

TEST(CompleteStateClone, InvalidStorageIdsCountsAndAliasingAreRejectedBeforeAccess) {
    CombatState source; Scratch scratch;
    ASSERT_EQ(initialize_combat(source, {}, scratch).status, StepStatus::awaiting_input);
    auto invalid = source;
    invalid.data.execution.overflow_id = 123;
    EXPECT_FALSE(valid_combat(invalid));
    invalid = source; invalid.data.execution.count = UINT32_MAX;
    EXPECT_FALSE(valid_combat(invalid));
    invalid = source; invalid.view().execution.reserve(100);
    invalid.data.seed.overflow_id = invalid.data.execution.overflow_id;
    EXPECT_FALSE(valid_combat(invalid));
}
} // namespace
} // namespace sts2::engine
