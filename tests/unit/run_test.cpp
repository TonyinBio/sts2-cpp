#include "sts2/engine/action.hpp"
#include "../../src/engine/card_state.hpp"
#include "../../src/engine/gameplay.hpp"
#include "../../src/engine/continuation_scenario.hpp"
#include "../../src/engine/parent_room_scenario.hpp"
#include "storage_allocator.hpp"
#include "sts2/json_io/session.hpp"
#include "sts2/observation/run.hpp"
#include "sts2/persistence/checkpoint.hpp"
#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>
#include <limits>
#include <algorithm>
#include <cstring>

namespace sts2::engine {
namespace {
using Json = nlohmann::json;
TEST(PublicRun, PermanentCreatureAttributesRemainVisibleAndSurviveCheckpoint) {
    RunState state; Scratch scratch;
    ASSERT_EQ(initialize_combat_scenario(state, {}, {}).status, StepStatus::awaiting_input);
    // Arrange a permanent maximum-HP effect until supported content exposes one.
    state.view().combat->player.max_hp += 5;
    EXPECT_EQ(observe(state).max_hp, 85);
    RunState restored;
    ASSERT_EQ(restore(restored, checkpoint(state).bytes, scratch), RestoreStatus::restored);
    EXPECT_EQ(observe(restored).max_hp, 85);
}
TEST(PublicRun, StaleCreatureHandlesAreRejectedBeforeViewsAreRead) {
    RunState state; Scratch scratch;
    ASSERT_EQ(initialize_combat_scenario(state, {}, {}).status, StepStatus::awaiting_input);
    ++state.data.player.generation;
    const auto before = state;
    EXPECT_FALSE(valid_run(state));
    EXPECT_FALSE(observe(state).valid);
    EXPECT_TRUE(get_actions(state).empty());
    const PlayerInput input = CombatInput{state.data.boundary, CombatInputKind::end_turn};
    EXPECT_EQ(validate_action(state, input), Restriction::invalid_state);
    EXPECT_EQ(apply_action(state, input, scratch).status, StepStatus::engine_error);
    EXPECT_EQ(advance_to_decision(state, scratch).status, StepStatus::engine_error);
    EXPECT_THROW(checkpoint(state), std::invalid_argument);
    EXPECT_EQ(state.data.player, before.data.player);
    EXPECT_EQ(state.data.boundary, before.data.boundary);
}
TEST(PublicRun, CreatureOverflowCloneAndRestoreKeepIndependentBranches) {
    RunState state; Scratch scratch;
    ASSERT_EQ(initialize_combat_scenario(state, {}, {}).status, StepStatus::awaiting_input);
    auto pool = state.view().creatures;
    const auto player = state.data.player;
    const auto enemy = state.view().combat.enemies.front();
    // Controlled retained operands force the exceptional creature storage path.
    for (int i = 0; i < 12; ++i) ASSERT_TRUE(pool.create({3, 3}, 4096).present());
    ASSERT_TRUE(valid_run(state));
    RunState cloned = state, restored;
    ASSERT_EQ(restore(restored, checkpoint(state).bytes, scratch), RestoreStatus::restored);
    EXPECT_EQ(restored, state);
    ASSERT_EQ(apply_action(cloned, CombatInput{cloned.data.boundary, CombatInputKind::end_turn}, scratch).status, StepStatus::awaiting_input);
    ASSERT_EQ(apply_action(restored, CombatInput{restored.data.boundary, CombatInputKind::end_turn}, scratch).status, StepStatus::awaiting_input);
    EXPECT_EQ(cloned, restored);
    EXPECT_NE(cloned, state);
    EXPECT_EQ(state.data.player, player);
    EXPECT_EQ(state.view().combat.enemies.front(), enemy);
}
TEST(PublicRun, DeadCreatureRemainsUntilCombatResolutionReleasesItsOperands) {
    RunState state; Scratch scratch;
    const std::array deck{CardSpec{CardId::strike}, CardSpec{CardId::strike}};
    ASSERT_EQ(initialize_combat_scenario(state, {}, deck, {}, {}, 1).status, StepStatus::awaiting_input);
    const auto enemy = state.view().combat.enemies.front();
    const auto player = state.data.player;
    auto terminal = state; terminal.view().direct_combat = true;
    const auto strike = state.view().combat->zone(Zone::hand).front();
    ASSERT_EQ(apply_action(terminal, CombatInput{terminal.data.boundary, CombatInputKind::play_card, strike, true}, scratch).status, StepStatus::combat_won);
    ASSERT_NE(terminal.view().creatures.get(enemy), nullptr);
    EXPECT_EQ(terminal.view().creatures.get(enemy)->hp, 0);
    RunState restored;
    ASSERT_EQ(restore(restored, checkpoint(terminal).bytes, scratch), RestoreStatus::restored);
    EXPECT_EQ(restored, terminal);
    ASSERT_EQ(apply_action(state, CombatInput{state.data.boundary, CombatInputKind::play_card, strike, true}, scratch).status, StepStatus::awaiting_input);
    EXPECT_FALSE(state.view().combat);
    EXPECT_FALSE(state.view().creatures.get(enemy));
    EXPECT_EQ(state.data.player, player);
    EXPECT_TRUE(state.view().creatures.slots()[enemy.slot].value.equals(state.overflow, CreatureState{}, state.overflow));
    ASSERT_EQ(restore(restored, checkpoint(state).bytes, scratch), RestoreStatus::restored);
    EXPECT_EQ(restored, state);
}
TEST(PublicRun, EachUnblockedAttackUpdatesTheCreatureHitCountOnce) {
    RunState state; Scratch scratch;
    const std::array deck{CardSpec{CardId::strike}, CardSpec{CardId::strike}};
    ASSERT_EQ(initialize_combat_scenario(state, {}, deck, {}, {}, 40).status, StepStatus::awaiting_input);
    for (unsigned hits = 1; hits <= 2; ++hits) {
        const auto card = state.view().combat->zone(Zone::hand).front();
        ASSERT_EQ(apply_action(state, CombatInput{state.data.boundary, CombatInputKind::play_card, card, true}, scratch).status, StepStatus::awaiting_input);
        EXPECT_EQ(state.view().combat->enemy.hp, 40 - 6 * static_cast<int>(hits));
        EXPECT_EQ(state.view().combat->enemy.positive_unblocked_hits, hits);
    }
}
struct NestedPromptInfo { PromptId previous; CardHandle source; };
void reach_nested_shuffle_prompt(RunState& state, std::span<const scenario::ShuffleReactionKind> hooks, NestedPromptInfo& info, std::span<const ContentId> relics = {}, std::uint32_t joss_counter = 0, bool remember = false) {
    Scratch scratch;
    const std::array deck{CardSpec{CardId::burning_pact}, CardSpec{CardId::defend}, CardSpec{CardId::strike}, CardSpec{CardId::strike}, CardSpec{CardId::defend}};
    ASSERT_EQ(initialize_combat_scenario(state, {}, deck, {}, relics, 0, joss_counter).status, StepStatus::awaiting_input);
    scenario::configure_shuffle_reactions(state, hooks, remember);
    auto play = [&](CardId definition) {
        for (auto card : state.view().combat->zone(Zone::hand)) {
            if (state.view().combat->cards.get(card)->definition == definition)
                return apply_action(state, CombatInput{state.data.boundary, CombatInputKind::play_card, card, false}, scratch).status;
        }
        return StepStatus::engine_error;
    };
    ASSERT_EQ(play(CardId::defend), StepStatus::awaiting_input);
    ASSERT_EQ(play(CardId::burning_pact), StepStatus::awaiting_input);
    const auto exhaust_prompt = state.view().combat->selection.id;
    info.previous = exhaust_prompt; info.source = state.view().combat->zone(Zone::play).front();
    const auto exhausted = state.view().combat->selection_candidates.front();
    ASSERT_EQ(apply_action(state, CombatInput{state.data.boundary, CombatInputKind::select_card, exhausted, false, exhaust_prompt}, scratch).status, StepStatus::awaiting_input);
    ASSERT_EQ(apply_action(state, CombatInput{state.data.boundary, CombatInputKind::confirm_selection, {}, false, exhaust_prompt}, scratch).status, StepStatus::awaiting_input);
    ASSERT_EQ(state.view().combat->phase, CombatPhase::card_choice);
}
StepStatus choose_nested_card(RunState& branch, Scratch& scratch, std::size_t index = 0) {
    const auto combat = *branch.view().combat;
    return apply_action(branch, CombatInput{branch.data.boundary, CombatInputKind::select_card, combat.selection_candidates[index], false, combat.selection.id}, scratch).status;
}
// Controlled architecture fixtures use existing compiled instructions and public
// stepping. They do not establish native support for additional power semantics.
TEST(PublicRun, CombinedCreatureAndPowerOverflowRestoreReacquiresCanonicalRecords) {
    // Keep retired allocations readable but poison their contents, so stale
    // borrowed references fail deterministically without depending on malloc reuse.
    struct RetiredBuffers {
        struct Buffer { void* pointer; std::size_t size, alignment; };
        std::array<Buffer, 128> retired {};
        std::size_t count = 0;
        ~RetiredBuffers() {
            for (std::size_t i = 0; i < count; ++i)
                StorageAllocator{}.deallocate(nullptr, retired[i].pointer, retired[i].size, retired[i].alignment);
        }
        StorageAllocator allocator() {
            return {this, StorageAllocator{}.allocate,
                [](void* context, void* pointer, std::size_t size, std::size_t alignment) {
                    auto& self = *static_cast<RetiredBuffers*>(context);
                    if (self.count == self.retired.size()) std::terminate();
                    std::memset(pointer, 0xa5, size);
                    self.retired[self.count++] = {pointer, size, alignment};
                }};
        }
    } buffers;
    RunState source; Scratch scratch;
    ASSERT_EQ(initialize_combat_scenario(source, {}, {}).status, StepStatus::awaiting_input);
    for (int i = 0; i < 12; ++i) ASSERT_TRUE(source.view().creatures.create({3, 3}, 4096).present());
    for (int i = 0; i < 9; ++i)
        gameplay::apply_power(*source.view().combat, scratch, PowerId::strength, 1, false, false, {}, true, false, PowerInstance::Stacking::separate);
    ASSERT_TRUE(source.data.creatures.slots.overflowed());
    ASSERT_TRUE(source.view().combat->player.powers.overflowed());
    ASSERT_TRUE(valid_run(source));
    RunState restored(buffers.allocator());
    ASSERT_EQ(restore(restored, checkpoint(source).bytes, scratch), RestoreStatus::restored);
    EXPECT_EQ(restored, source);
}

TEST(PublicRun, DistinctAndPerApplierPowersSurviveOverflowCloneAndRestore) {
    RunState state; Scratch scratch;
    const std::array deck{CardSpec{CardId::strike}, CardSpec{CardId::strike}, CardSpec{CardId::strike}, CardSpec{CardId::strike}, CardSpec{CardId::strike}};
    ASSERT_EQ(initialize_combat_scenario(state, {}, deck).status, StepStatus::awaiting_input);
    auto combat = *state.view().combat;
    using Stacking = PowerInstance::Stacking;
    std::array<std::uint32_t, 9> separate {};
    for (auto& id : separate) id = gameplay::apply_power(combat, scratch, PowerId::strength, 1, false, false, {}, true, false, Stacking::separate, combat.player_handle);
    const auto player = gameplay::apply_power(combat, scratch, PowerId::strength, 2, false, false, {}, true, false, Stacking::by_applier, combat.player_handle);
    const auto enemy = gameplay::apply_power(combat, scratch, PowerId::strength, 3, false, false, {}, true, true, Stacking::by_applier, combat.enemy_handle);
    EXPECT_NE(player, enemy);
    EXPECT_EQ(gameplay::apply_power(combat, scratch, PowerId::strength, 1, false, false, {}, true, false, Stacking::by_applier, combat.player_handle), player);
    gameplay::apply_power(combat, scratch, PowerId::strength, 4, false, false);
    ASSERT_TRUE(combat.player.powers.overflowed());
    ASSERT_TRUE(valid_run(state));
    EXPECT_EQ(combat.player.power_instances(combat.creature_overflow).size(), 12u);
    EXPECT_EQ(combat.player.power_amount(PowerId::strength, combat.creature_overflow), 19);
    const auto saved = checkpoint(state).bytes;
    RunState clone = state, restored;
    ASSERT_EQ(restore(restored, saved, scratch), RestoreStatus::restored);
    EXPECT_EQ(restored, state);
    CombatState extracted{ConstCombatView(combat)}, extracted_copy;
    ASSERT_TRUE(valid_combat(extracted));
    ASSERT_EQ(clone_into(extracted_copy, extracted), CloneStatus::cloned);
    EXPECT_EQ(extracted_copy, extracted);
    for (auto* branch : {&clone, &restored}) {
        const auto view = *branch->view().combat;
        const auto card = view.zone(Zone::hand).front();
        ASSERT_EQ(apply_action(*branch, CombatInput{branch->data.boundary, CombatInputKind::play_card, card, true}, scratch).status, StepStatus::awaiting_input);
        EXPECT_EQ(branch->view().combat->enemy.hp, combat.enemy.hp - 25);
        branch->view().combat->player.find_power_instance(separate[0], branch->overflow)->amount = 2;
    }
    EXPECT_EQ(clone, restored);
    EXPECT_EQ(checkpoint(state).bytes, saved);
}

TEST(PublicRun, PerApplierIdentityAndArtifactRemainOnRetainedCreature) {
    RunState state; Scratch scratch;
    ASSERT_EQ(initialize_combat_scenario(state, {}, {}).status, StepStatus::awaiting_input);
    auto combat = *state.view().combat;
    const auto retained = combat.enemy_handle;
    gameplay::apply_power(combat, scratch, PowerId::strength, 2, false, false, {}, true, true, PowerInstance::Stacking::by_applier, retained);
    gameplay::apply_power(combat, scratch, PowerId::artifact, 1, true, false, {}, true, false, PowerInstance::Stacking::separate);
    const auto replacement = state.view().creatures.create({40, 40}, 4096);
    state.view().combat.enemies[0] = replacement;
    gameplay::apply_power(*state.view().combat, scratch, PowerId::artifact, 2, true, false);
    ASSERT_TRUE(valid_run(state));
    EXPECT_THROW(CombatState{ConstCombatView(*state.view().combat)}, std::invalid_argument);
    RunState clone = state, restored;
    ASSERT_EQ(restore(restored, checkpoint(state).bytes, scratch), RestoreStatus::restored);
    EXPECT_EQ(restored, state);
    for (auto* branch : {&clone, &restored}) {
        gameplay::apply_power(*branch->view().combat, scratch, PowerId::weak, 2, true, false, {}, true, false, PowerInstance::Stacking::by_type, {}, retained);
        EXPECT_EQ(branch->view().creatures.get(retained)->power_amount(PowerId::artifact, branch->overflow), 0);
        EXPECT_EQ(branch->view().creatures.get(retained)->power_amount(PowerId::weak, branch->overflow), 0);
        EXPECT_EQ(branch->view().creatures.get(replacement)->power_amount(PowerId::artifact, branch->overflow), 2);
        EXPECT_EQ(advance_to_decision(*branch, scratch).status, StepStatus::awaiting_input);
    }
    EXPECT_EQ(clone, restored);
    EXPECT_EQ(state.view().creatures.get(retained)->power_amount(PowerId::artifact, state.overflow), 1);
}

TEST(PublicRun, CapturedCreatureTargetSurvivesMembershipReplacementAcrossNestedPrompt) {
    RunState state; Scratch scratch; NestedPromptInfo info;
    const std::array hooks{scenario::ShuffleReactionKind::choose_card};
    reach_nested_shuffle_prompt(state, hooks, info);
    const auto captured = state.view().combat.enemies.front();
    const auto captured_hp = state.view().creatures.get(captured)->hp;
    // Resume an existing damage program after the controlled interruption.
    auto combat = *state.view().combat;
    combat.cards.get(info.source)->definition = CardId::strike;
    combat.cards.get(info.source)->origin = {};
    for (auto& frame : combat.execution) if (std::holds_alternative<std::monostate>(frame.pending)) {
        frame.instruction = 0; frame.target = captured;
    }
    const auto replacement = state.view().creatures.create({40, 40}, 4096);
    state.view().combat.enemies[0] = replacement;
    ASSERT_TRUE(valid_run(state));
    EXPECT_THROW(CombatState{ConstCombatView(*state.view().combat)}, std::invalid_argument);
    const auto saved = checkpoint(state).bytes;
    RunState clone = state, restored;
    ASSERT_EQ(restore(restored, saved, scratch), RestoreStatus::restored);
    for (auto* branch : {&clone, &restored}) {
        ASSERT_EQ(choose_nested_card(*branch, scratch), StepStatus::awaiting_input);
        EXPECT_EQ(branch->view().creatures.get(captured)->hp, captured_hp - 6);
        EXPECT_EQ(branch->view().creatures.get(replacement)->hp, 40);
    }
    EXPECT_EQ(clone, restored);
    EXPECT_EQ(checkpoint(state).bytes, saved);
    RunState stale = state;
    ASSERT_TRUE(stale.view().creatures.erase(captured));
    const auto reused = stale.view().creatures.create({40, 40}, 4096);
    ASSERT_EQ(reused.slot, captured.slot); ASSERT_NE(reused.generation, captured.generation);
    EXPECT_FALSE(valid_run(stale));
    EXPECT_THROW(checkpoint(stale), std::invalid_argument);
    const auto unchanged = stale;
    const auto stale_view = *stale.view().combat;
    const CombatInput stale_input{stale.data.boundary, CombatInputKind::select_card, stale_view.selection_candidates.front(), false, stale_view.selection.id};
    EXPECT_EQ(validate_action(stale, stale_input), Restriction::invalid_state);
    EXPECT_EQ(apply_action(stale, stale_input, scratch).reason, Restriction::invalid_state);
    EXPECT_EQ(stale, unchanged);
    EXPECT_EQ(stale.view().creatures.get(reused)->hp, 40);
    EXPECT_EQ(stale.view().creatures.get(replacement)->hp, 40);
}

TEST(PublicRun, SuspendedCombatExtractionPreservesCapturedCreatureIdentities) {
    RunState state; Scratch scratch; NestedPromptInfo info;
    const std::array hooks{scenario::ShuffleReactionKind::choose_card};
    reach_nested_shuffle_prompt(state, hooks, info);
    const auto replacement = state.view().creatures.create({40, 40}, 4096);
    state.view().combat.enemies[0] = replacement;
    auto combat = *state.view().combat;
    combat.cards.get(info.source)->definition = CardId::strike;
    combat.cards.get(info.source)->origin = {};
    for (auto& frame : combat.execution) if (std::holds_alternative<std::monostate>(frame.pending)) { frame.instruction = 0; frame.target = replacement; }
    CombatState extracted{ConstCombatView(combat)}, restored;
    ASSERT_TRUE(valid_combat(extracted));
    ASSERT_EQ(restore(restored, checkpoint(extracted).bytes, scratch), RestoreStatus::restored);
    const auto choose = [&](CombatState& branch) {
        const auto view = branch.view();
        return apply_combat(branch, {view.boundary, CombatInputKind::select_card, view.selection_candidates.front(), false, view.selection.id}, scratch).status;
    };
    EXPECT_EQ(choose(extracted), StepStatus::awaiting_input);
    EXPECT_EQ(choose(restored), StepStatus::awaiting_input);
    EXPECT_EQ(extracted.enemy.hp, 34);
    EXPECT_EQ(extracted, restored);
}

TEST(PublicRun, NestedShuffleChoiceResumesDrawAndParentExactlyOnceAfterRestore) {
    RunState state; Scratch scratch; NestedPromptInfo info;
    const std::array hooks{scenario::ShuffleReactionKind::choose_card};
    reach_nested_shuffle_prompt(state, hooks, info);
    const auto exhaust_prompt = info.previous;
    ASSERT_EQ(state.view().combat->phase, CombatPhase::card_choice);
    EXPECT_NE(state.view().combat->selection.id, exhaust_prompt);
    EXPECT_EQ(state.view().combat->counters.finished_plays, 1u);
    EXPECT_EQ(state.view().combat->counters.non_hand_draws_this_turn, 0u);
    const auto saved = checkpoint(state).bytes;
    RunState clone = state, restored;
    ASSERT_EQ(restore(restored, saved, scratch), RestoreStatus::restored);
    EXPECT_EQ(advance_to_decision(state, scratch).status, StepStatus::awaiting_input);
    EXPECT_EQ(checkpoint(state).bytes, saved);
    auto choose = [&](RunState& branch, std::size_t index) {
        const auto combat = *branch.view().combat;
        return apply_action(branch, CombatInput{branch.data.boundary, CombatInputKind::select_card, combat.selection_candidates[index], false, combat.selection.id}, scratch).status;
    };
    ASSERT_EQ(choose(clone, 0), StepStatus::awaiting_input);
    ASSERT_EQ(choose(restored, 0), StepStatus::awaiting_input);
    EXPECT_EQ(clone, restored);
    EXPECT_EQ(clone.view().combat->phase, CombatPhase::play);
    EXPECT_EQ(clone.view().combat->counters.finished_plays, 2u);
    EXPECT_EQ(clone.view().combat->counters.non_hand_draws_this_turn, 2u);
    EXPECT_EQ(checkpoint(state).bytes, saved);
    RunState alternate = state;
    ASSERT_EQ(choose(alternate, 1), StepStatus::awaiting_input);
    EXPECT_NE(alternate, clone);
    EXPECT_EQ(alternate.view().rng, clone.view().rng);
}
TEST(PublicRun, CapturedShuffleListenersRejectReplacementsAndExcludeNewListeners) {
    RunState state; Scratch scratch; NestedPromptInfo info;
    const std::array hooks{scenario::ShuffleReactionKind::choose_card, scenario::ShuffleReactionKind::nested_draw};
    reach_nested_shuffle_prompt(state, hooks, info);
    ASSERT_EQ(state.view().combat->phase, CombatPhase::card_choice);
    auto combat = *state.view().combat;
    const auto removed = combat.shuffle_order.back();
    combat.shuffle_order.pop_back(); ASSERT_TRUE(combat.shuffle_reactions.erase(removed));
    const auto replacement = combat.shuffle_reactions.create({ShuffleReactionKind::nested_draw, false}, 4096);
    ASSERT_TRUE(replacement.present()); combat.shuffle_order.push_back(replacement);
    ASSERT_EQ(replacement.slot, removed.slot); EXPECT_NE(replacement.generation, removed.generation);
    ASSERT_TRUE(valid_run(state));
    const auto saved = checkpoint(state).bytes;
    RunState clone = state, restored;
    ASSERT_EQ(restore(restored, saved, scratch), RestoreStatus::restored);
    ASSERT_EQ(choose_nested_card(clone, scratch), StepStatus::awaiting_input);
    ASSERT_EQ(choose_nested_card(restored, scratch), StepStatus::awaiting_input);
    EXPECT_EQ(clone, restored);
    EXPECT_EQ(clone.view().combat->counters.non_hand_draws_this_turn, 2u);
    EXPECT_FALSE(clone.view().combat->shuffle_reactions.get(replacement)->fired);
    EXPECT_EQ(checkpoint(state).bytes, saved);
}
TEST(PublicRun, NestedContinuationOverflowKeepsIndependentSameAndAlternativeFutures) {
    RunState state; Scratch scratch; NestedPromptInfo info;
    std::array<scenario::ShuffleReactionKind, 7> hooks;
    hooks.fill(scenario::ShuffleReactionKind::nested_draw); hooks.back() = scenario::ShuffleReactionKind::choose_card;
    reach_nested_shuffle_prompt(state, hooks, info);
    ASSERT_EQ(state.view().combat->phase, CombatPhase::card_choice);
    ASSERT_TRUE(state.view().combat->data.execution.overflowed());
    ASSERT_TRUE(valid_run(state));
    const auto saved = checkpoint(state).bytes;
    RunState clone = state, restored;
    ASSERT_EQ(restore(restored, saved, scratch), RestoreStatus::restored);
    ASSERT_EQ(choose_nested_card(clone, scratch), StepStatus::awaiting_input);
    ASSERT_EQ(choose_nested_card(restored, scratch), StepStatus::awaiting_input);
    EXPECT_EQ(clone, restored);
    EXPECT_EQ(clone.view().combat->phase, CombatPhase::play);
    EXPECT_EQ(clone.view().combat->counters.finished_plays, 2u);
    RunState alternate = state;
    ASSERT_EQ(choose_nested_card(alternate, scratch, 1), StepStatus::awaiting_input);
    EXPECT_NE(alternate, clone); EXPECT_EQ(alternate.view().rng, clone.view().rng);
    EXPECT_EQ(checkpoint(state).bytes, saved);
}
TEST(PublicRun, NestedPromptRetainsSourceAfterItLeavesActiveCardMembership) {
    RunState state; Scratch scratch; NestedPromptInfo info;
    const std::array hooks{scenario::ShuffleReactionKind::choose_card};
    reach_nested_shuffle_prompt(state, hooks, info);
    ASSERT_EQ(state.view().combat->phase, CombatPhase::card_choice);
    auto play = state.view().combat->zone(Zone::play);
    play.erase(std::find(play.begin(), play.end(), info.source));
    state.view().combat->cards.get(info.source)->removed = true;
    ASSERT_TRUE(valid_run(state));
    RunState restored;
    ASSERT_EQ(restore(restored, checkpoint(state).bytes, scratch), RestoreStatus::restored);
    ASSERT_EQ(choose_nested_card(restored, scratch), StepStatus::awaiting_input);
    EXPECT_EQ(restored.view().combat->counters.finished_plays, 2u);
    EXPECT_NE(restored.view().combat->cards.get(info.source), nullptr);
    EXPECT_TRUE(restored.view().combat->cards.get(info.source)->removed);
    EXPECT_EQ(state.view().combat->phase, CombatPhase::card_choice);
}
TEST(PublicRun, CommonNestedPromptCloneUsesNoAllocationAndBudgetFailureIsOperational) {
    test::AllocationCounter allocation;
    RunState state(allocation.allocator()), clone(allocation.allocator()); Scratch scratch; NestedPromptInfo info;
    const std::array hooks{scenario::ShuffleReactionKind::choose_card};
    reach_nested_shuffle_prompt(state, hooks, info);
    ASSERT_EQ(state.view().combat->phase, CombatPhase::card_choice);
    ASSERT_TRUE(state.combat_overflow.empty());
    const auto saved = checkpoint(state).bytes;
    const auto allocations = allocation.allocations;
    allocation.fail_after = allocations;
    ASSERT_EQ(clone_into(clone, state), CloneStatus::cloned);
    EXPECT_EQ(allocation.allocations, allocations);
    scratch.step_limit = 1;
    EXPECT_EQ(choose_nested_card(clone, scratch), StepStatus::engine_error);
    EXPECT_EQ(clone.view().phase, RunPhase::invalid);
    EXPECT_FALSE(observe(clone).valid);
    EXPECT_EQ(checkpoint(state).bytes, saved);
}
TEST(PublicRun, NestedPromptRejectsMalformedResultDestinationBeforePublishingQueries) {
    RunState state; Scratch scratch; NestedPromptInfo info;
    const std::array hooks{scenario::ShuffleReactionKind::choose_card};
    reach_nested_shuffle_prompt(state, hooks, info);
    ASSERT_EQ(state.view().combat->phase, CombatPhase::card_choice);
    ++state.view().combat->selection.continuation_depth;
    EXPECT_FALSE(valid_run(state));
    const auto boundary = state.data.boundary;
    const auto candidate = state.view().combat->selection_candidates.front();
    const PlayerInput input = CombatInput{boundary, CombatInputKind::select_card, candidate, false, state.view().combat->selection.id};
    EXPECT_EQ(validate_action(state, input), Restriction::invalid_state);
    EXPECT_THROW(checkpoint(state), std::invalid_argument);
    EXPECT_TRUE(get_actions(state).empty());
    EXPECT_FALSE(observe(state).valid);
    EXPECT_EQ(state.data.boundary, boundary);
}
TEST(PublicRun, OverflowingNestedCloneAndRestoreFailuresKeepDestinationTransactional) {
    test::AllocationCounter allocation;
    {
        RunState state(allocation.allocator()), destination(allocation.allocator()); Scratch scratch; NestedPromptInfo info;
        std::array<scenario::ShuffleReactionKind, 7> hooks;
        hooks.fill(scenario::ShuffleReactionKind::nested_draw); hooks.back() = scenario::ShuffleReactionKind::choose_card;
        reach_nested_shuffle_prompt(state, hooks, info);
        ASSERT_EQ(state.view().combat->phase, CombatPhase::card_choice);
        const auto encoded = checkpoint(state).bytes;
        ASSERT_EQ(initialize_seeded_combat(destination, {}).status, StepStatus::awaiting_input);
        const auto before = checkpoint(destination).bytes;
        allocation.fail_after = allocation.allocations;
        EXPECT_EQ(clone_into(destination, state), CloneStatus::resource_limit);
        EXPECT_EQ(checkpoint(destination).bytes, before);
        EXPECT_EQ(restore(destination, encoded, scratch), RestoreStatus::resource_limit);
        EXPECT_EQ(checkpoint(destination).bytes, before);
        EXPECT_EQ(checkpoint(state).bytes, encoded);
    }
    EXPECT_EQ(allocation.outstanding, 0u);
}
TEST(PublicRun, NestedRelicDrawDefersItsCounterResetUntilTheChildChoiceFinishes) {
    RunState state; Scratch scratch; NestedPromptInfo info;
    const std::array hooks{scenario::ShuffleReactionKind::choose_card};
    const std::array relics{SetupRules::find("RELIC.JOSS_PAPER")};
    reach_nested_shuffle_prompt(state, hooks, info, relics, 4);
    ASSERT_EQ(state.view().combat->phase, CombatPhase::card_choice);
    EXPECT_EQ(state.view().combat->joss_paper_counter, 5u);
    EXPECT_EQ(state.view().joss_paper_counter, 5u);
    EXPECT_EQ(state.view().combat->counters.finished_plays, 1u);
    const auto saved = checkpoint(state).bytes;
    RunState restored;
    ASSERT_EQ(restore(restored, saved, scratch), RestoreStatus::restored);
    ASSERT_EQ(choose_nested_card(restored, scratch), StepStatus::awaiting_input);
    EXPECT_EQ(restored.view().combat->phase, CombatPhase::play);
    EXPECT_EQ(restored.view().combat->joss_paper_counter, 0u);
    EXPECT_EQ(restored.view().joss_paper_counter, 0u);
    EXPECT_EQ(restored.view().combat->counters.finished_plays, 2u);
    EXPECT_EQ(checkpoint(state).bytes, saved);
}
TEST(PublicRun, DirectActionValidationDoesNotCloneOverflowState) {
    test::AllocationCounter allocation;
    RunState state(allocation.allocator()); Scratch scratch;
    const std::array<CardSpec, 200> deck{};
    ASSERT_EQ(initialize_combat_scenario(state, {}, deck).status, StepStatus::awaiting_input);
    ASSERT_FALSE(state.combat_overflow.empty());
    const auto before = checkpoint(state).bytes;
    const auto allocations = allocation.allocations;
    allocation.fail_after = allocations;
    const auto card = state.view().combat->zone(Zone::hand).front();
    const PlayerInput input = CombatInput{state.data.boundary, CombatInputKind::play_card, card, true};
    EXPECT_NO_THROW(EXPECT_EQ(validate_action(state, input), Restriction::none));
    EXPECT_EQ(allocation.allocations, allocations);
    EXPECT_EQ(checkpoint(state).bytes, before);
}
TEST(PublicRun, BorrowedRunActionsTraverseOverflowWithoutAllocationAndExpireAfterMutation) {
    test::AllocationCounter allocation;
    RunState state(allocation.allocator()); Scratch scratch;
    const std::array<CardSpec, 200> deck{};
    ASSERT_EQ(initialize_combat_scenario(state, {}, deck).status, StepStatus::awaiting_input);
    const auto saved = checkpoint(state).bytes;
    const auto allocations = allocation.allocations;
    allocation.fail_after = allocations;
    const RunActionSpaceView first(state);
    ASSERT_EQ(first.size(), 6u);
    std::size_t plays = 0, turns = 0;
    for (std::size_t i = 0; i < first.size(); ++i) {
        const auto action = first.at(i);
        ASSERT_TRUE(action);
        const auto& input = std::get<CombatInput>(action->input);
        plays += input.kind == CombatInputKind::play_card;
        turns += input.kind == CombatInputKind::end_turn;
        EXPECT_EQ(action->restriction, Restriction::none);
    }
    EXPECT_EQ(plays, 5u); EXPECT_EQ(turns, 1u);
    EXPECT_FALSE(first.at(first.size()));
    EXPECT_EQ(allocation.allocations, allocations);
    EXPECT_EQ(checkpoint(state).bytes, saved);
    for (int i = 0; i < 3; ++i) {
        const RunActionSpaceView current(state);
        ASSERT_EQ(apply_action(state, current.at(0)->input, scratch).status, StepStatus::awaiting_input);
    }
    EXPECT_TRUE(first.empty());
    const RunActionSpaceView exhausted_energy(state);
    ASSERT_EQ(exhausted_energy.size(), 3u);
    EXPECT_EQ(exhausted_energy.at(0)->restriction, Restriction::insufficient_energy);
    EXPECT_EQ(exhausted_energy.at(1)->restriction, Restriction::insufficient_energy);
    EXPECT_EQ(std::get<CombatInput>(exhausted_energy.at(2)->input).kind, CombatInputKind::end_turn);
    EXPECT_EQ(exhausted_energy.at(2)->restriction, Restriction::none);
    EXPECT_EQ(allocation.allocations, allocations);
}
TEST(PublicRun, ParentInteractionReturnsAfterChildCombatAndRewardsExactlyOnce) {
    RunState state; Scratch scratch;
    const std::array deck{CardSpec{CardId::strike}, CardSpec{CardId::strike}};
    ASSERT_EQ(initialize_combat_scenario(state, {}, deck, {}, {}, 1).status, StepStatus::awaiting_input);
    scenario::configure_parent_return(state, 7);
    const auto gold = observe(state).gold;
    const auto card = state.view().combat->zone(Zone::hand).front();
    ASSERT_EQ(apply_action(state, CombatInput{state.data.boundary, CombatInputKind::play_card, card, true}, scratch).status, StepStatus::awaiting_input);
    ASSERT_EQ(state.view().phase, RunPhase::rewards);
    const auto reward_boundary = state.data.boundary;
    const auto reward_rng = state.view().player_rng;
    const auto saved_rewards = checkpoint(state).bytes;
    ASSERT_EQ(apply_action(state, RunInput{state.data.boundary, RunInputKind::skip_rewards}, scratch).status, StepStatus::awaiting_input);
    const auto view = observe(state);
    ASSERT_EQ(view.options.size(), 2u);
    EXPECT_EQ(describe(view.options[0]).name, "Continue");
    EXPECT_EQ(view.options[0].gold_reward, 7);
    EXPECT_EQ(describe(view.options[0]).description, "Gain 7 Gold.");
    EXPECT_FALSE(state.view().combat); EXPECT_FALSE(state.view().rewards);
    const RunActionSpaceView actions(state); ASSERT_EQ(actions.size(), 2u);
    const auto saved_parent = checkpoint(state).bytes;
    EXPECT_EQ(advance_to_decision(state, scratch).status, StepStatus::awaiting_input);
    EXPECT_EQ(checkpoint(state).bytes, saved_parent);
    EXPECT_EQ(validate_action(state, RunInput{reward_boundary, RunInputKind::skip_rewards}), Restriction::stale_boundary);
    RunState clone = state, restored, parent_restored;
    ASSERT_EQ(restore(parent_restored, saved_parent, scratch), RestoreStatus::restored);
    EXPECT_EQ(parent_restored, state);
    ASSERT_EQ(restore(restored, saved_rewards, scratch), RestoreStatus::restored);
    ASSERT_EQ(apply_action(restored, RunInput{restored.data.boundary, RunInputKind::skip_rewards}, scratch).status, StepStatus::awaiting_input);
    EXPECT_EQ(restored, state);
    ASSERT_EQ(apply_action(clone, actions.at(0)->input, scratch).status, StepStatus::awaiting_input);
    ASSERT_EQ(apply_action(restored, actions.at(0)->input, scratch).status, StepStatus::awaiting_input);
    EXPECT_EQ(clone, restored);
    ASSERT_EQ(apply_action(parent_restored, actions.at(0)->input, scratch).status, StepStatus::awaiting_input);
    EXPECT_EQ(parent_restored, clone);
    EXPECT_EQ(observe(clone).gold, gold + 7);
    EXPECT_EQ(clone.view().phase, RunPhase::map);
    EXPECT_EQ(clone.view().player_rng, reward_rng);
    const auto completed = checkpoint(clone).bytes;
    EXPECT_EQ(advance_to_decision(clone, scratch).status, StepStatus::awaiting_input);
    EXPECT_EQ(checkpoint(clone).bytes, completed);
    EXPECT_EQ(apply_action(clone, actions.at(0)->input, scratch).reason, Restriction::stale_boundary);
    EXPECT_EQ(observe(clone).gold, gold + 7);
    RunState alternative = state;
    ASSERT_EQ(apply_action(alternative, actions.at(1)->input, scratch).status, StepStatus::awaiting_input);
    EXPECT_EQ(observe(alternative).gold, gold);
    EXPECT_NE(alternative, restored);
    EXPECT_EQ(checkpoint(state).bytes, saved_parent);
}
TEST(PublicRun, TypedCardChoiceMemoryClonesRestoresAndStaysOutOfThePermanentDeck) {
    RunState state; Scratch scratch; NestedPromptInfo info;
    const std::array hooks{scenario::ShuffleReactionKind::choose_card};
    reach_nested_shuffle_prompt(state, hooks, info, {}, 0, true);
    ASSERT_EQ(state.view().combat->phase, CombatPhase::card_choice);
    state.view().combat->cards.reserve(200);
    ASSERT_TRUE(state.view().combat->data.cards.slots.overflowed());
    const auto first = state.view().combat->selection_candidates[0];
    const auto second = state.view().combat->selection_candidates[1];
    const auto permanent = observe(state).deck;
    const auto saved = checkpoint(state).bytes;
    RunState clone = state, restored;
    ASSERT_EQ(restore(restored, saved, scratch), RestoreStatus::restored);
    ASSERT_EQ(choose_nested_card(clone, scratch), StepStatus::awaiting_input);
    ASSERT_EQ(choose_nested_card(restored, scratch), StepStatus::awaiting_input);
    EXPECT_EQ(clone, restored);
    const auto remembered = inspect_card(*clone.view().combat, info.source);
    ASSERT_TRUE(remembered); EXPECT_EQ(remembered->remembered_choice, first);
    EXPECT_EQ(observe(clone).deck, permanent);
    RunState after;
    ASSERT_EQ(restore(after, checkpoint(clone).bytes, scratch), RestoreStatus::restored);
    EXPECT_EQ(inspect_card(*after.view().combat, info.source)->remembered_choice, first);
    RunState alternate = state;
    ASSERT_EQ(choose_nested_card(alternate, scratch, 1), StepStatus::awaiting_input);
    EXPECT_EQ(inspect_card(*alternate.view().combat, info.source)->remembered_choice, second);
    EXPECT_NE(alternate, clone); EXPECT_EQ(alternate.view().rng, clone.view().rng);
    EXPECT_EQ(checkpoint(state).bytes, saved);
    clone.view().combat->enemy.hp = 1;
    bool finished = false;
    const RunActionSpaceView lethal_actions(clone);
    for (std::size_t i = 0; i < lethal_actions.size(); ++i) {
        const auto action = *lethal_actions.at(i);
        const auto* input = std::get_if<CombatInput>(&action.input);
        if (action.restriction == Restriction::none && input && input->kind == CombatInputKind::play_card && input->target_enemy) {
            ASSERT_EQ(apply_action(clone, action.input, scratch).status, StepStatus::awaiting_input);
            finished = true; break;
        }
    }
    ASSERT_TRUE(finished); ASSERT_EQ(clone.view().phase, RunPhase::rewards);
    ASSERT_EQ(apply_action(clone, RunInput{clone.data.boundary, RunInputKind::skip_rewards}, scratch).status, StepStatus::awaiting_input);
    ASSERT_EQ(enter_combat_scenario(clone, clone.data.boundary, "ENCOUNTER.SHRINKER_BEETLE_WEAK").status, StepStatus::awaiting_input);
    EXPECT_TRUE(clone.combat_overflow.empty());
    for (auto card : clone.view().combat->zone(Zone::hand)) EXPECT_FALSE(inspect_card(*clone.view().combat, card)->remembered_choice);
    EXPECT_EQ(observe(clone).deck, permanent);
    EXPECT_EQ(checkpoint(state).bytes, saved);
}
TEST(PublicRun, RememberedCardChoiceRejectsARecycledTargetGeneration) {
    RunState state; Scratch scratch; NestedPromptInfo info;
    const std::array hooks{scenario::ShuffleReactionKind::choose_card};
    reach_nested_shuffle_prompt(state, hooks, info, {}, 0, true);
    ASSERT_EQ(state.view().combat->phase, CombatPhase::card_choice);
    const auto target = state.view().combat->selection_candidates.front();
    ASSERT_EQ(choose_nested_card(state, scratch), StepStatus::awaiting_input);
    auto combat = *state.view().combat;
    const auto value = *combat.cards.get(target);
    ASSERT_TRUE(combat.cards.erase(target));
    const auto replacement = combat.cards.create(value, 4096);
    ASSERT_EQ(replacement.slot, target.slot); EXPECT_NE(replacement.generation, target.generation);
    for (auto zone : combat.zone_views()) for (auto& card : zone) if (card == target) card = replacement;
    EXPECT_FALSE(valid_run(state));
    EXPECT_THROW(checkpoint(state), std::invalid_argument);
    EXPECT_TRUE(RunActionSpaceView(state).empty());
    const auto boundary = state.data.boundary;
    const auto rng = state.view().rng;
    const PlayerInput input = CombatInput{boundary, CombatInputKind::end_turn};
    EXPECT_EQ(validate_action(state, input), Restriction::invalid_state);
    EXPECT_EQ(apply_action(state, input, scratch).reason, Restriction::invalid_state);
    EXPECT_EQ(state.data.boundary, boundary); EXPECT_EQ(state.view().rng, rng);
    // With only the stale memory removed, the replacement is a valid instance.
    combat.cards.get(info.source)->extra = std::monostate{};
    EXPECT_TRUE(valid_run(state));
}
TEST(PublicRun, ControlledShuffleStateCannotBeExportedAsNativeDiagnosticEvidence) {
    RunState state; NestedPromptInfo info;
    const std::array hooks{scenario::ShuffleReactionKind::choose_card};
    reach_nested_shuffle_prompt(state, hooks, info, {}, 0, true);
    ASSERT_EQ(state.view().combat->phase, CombatPhase::card_choice);
    NativeRunExporter exporter;
    try {
        (void)exporter.snapshot(state);
        FAIL() << "Controlled shuffle state must have an explicit diagnostic mapping";
    } catch (const std::invalid_argument& error) {
        EXPECT_NE(std::string_view(error.what()).find("native diagnostic mapping"), std::string_view::npos);
    }
}
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
    const auto player_identity = state.data.player;
    const auto retired_generation = state.data.creatures.highest_generation;
    NativeRunExporter exporter;
    ASSERT_EQ(enter_combat_scenario(state, state.view().boundary, "ENCOUNTER.SHRINKER_BEETLE_WEAK", exporter.trace()).status, StepStatus::awaiting_input);
    EXPECT_EQ(state.view().combat_generation, 2u);
    EXPECT_EQ(state.data.player, player_identity);
    EXPECT_GE(state.view().combat.enemies.front().generation, retired_generation);
    EXPECT_FALSE(state.view().creatures.get({state.view().combat.enemies.front().slot, 1}));
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
TEST(PublicRun, DiagnosticSnapshotKeepsOriginlessCombatCardIndependentOfDeck) {
    const std::array deck{CardSpec{CardId::strike}, CardSpec{CardId::defend}};
    RunState state; NativeRunExporter exporter;
    ASSERT_EQ(initialize_combat_scenario(state, {}, deck, exporter.trace()).status, StepStatus::awaiting_input);
    auto combat = *state.view().combat;
    const auto handle = combat.zone(Zone::hand).front();
    auto* card = combat.cards.get(handle);
    ASSERT_NE(card, nullptr);
    card->origin = {};
    ASSERT_TRUE(valid_run(state));
    const auto snapshot = exporter.snapshot(state);
    const auto& exported = snapshot["combat"]["cards"]["card_" + std::to_string(handle.slot)];
    EXPECT_TRUE(exported["deck_version"].is_null());
}
TEST(PublicRun, ReorderingDeckPreservesCombatOriginsThroughCheckpoint) {
    const std::array deck{CardSpec{CardId::strike}, CardSpec{CardId::defend}};
    RunState state; Scratch scratch; NativeRunExporter exporter;
    ASSERT_EQ(initialize_combat_scenario(state, {}, deck, exporter.trace()).status, StepStatus::awaiting_input);
    std::swap(state.view().deck.handles()[0], state.view().deck.handles()[1]);
    ASSERT_TRUE(valid_run(state));
    const auto saved = checkpoint(state).bytes;
    RunState restored;
    ASSERT_EQ(restore(restored, saved, scratch), RestoreStatus::restored);
    EXPECT_EQ(restored, state);
    const auto snapshot = exporter.snapshot(state);
    for (std::size_t i = 0; i < state.view().combat->cards.slots().size(); ++i) {
        const auto& slot = state.view().combat->cards.slots()[i];
        if (!slot.live) continue;
        const auto index = slot.value.origin == state.view().deck.handles()[0] ? 0 : 1;
        EXPECT_EQ(snapshot["combat"]["cards"]["card_" + std::to_string(i)]["deck_version"], "/players/0/deck/" + std::to_string(index));
    }
}
TEST(PublicRun, OrderedCostChangesSurviveRestorationAndExpireAtTurnEnd) {
    const std::array deck{CardSpec{CardId::strike}};
    RunState state; Scratch scratch; NativeRunExporter exporter;
    ASSERT_EQ(initialize_combat_scenario(state, {}, deck, exporter.trace()).status, StepStatus::awaiting_input);
    const auto card = observe(state).hand_cards.front().handle;
    auto combat = *state.view().combat;
    combat.cost_modifiers.push_back({card, CostModifierKind::absolute, CostExpiry::combat, 3});
    combat.cost_modifiers.push_back({card, CostModifierKind::relative, CostExpiry::turn, -2});
    combat.cost_modifiers.push_back({card, CostModifierKind::relative, CostExpiry::combat, 1});
    EXPECT_EQ(observe(state).hand_cards.front().cost, 2);
    const auto snapshot = exporter.snapshot(state);
    const auto& modifiers = snapshot["combat"]["cards"]["card_" + std::to_string(card.slot)]["energy_cost"]["local_modifiers"];
    ASSERT_EQ(modifiers.size(), 3u);
    EXPECT_EQ(modifiers[1]["expiration"], 2);
    EXPECT_EQ(modifiers[1]["type"], "Relative");
    RunState restored;
    ASSERT_EQ(restore(restored, checkpoint(state).bytes, scratch), RestoreStatus::restored);
    EXPECT_EQ(observe(restored).hand_cards.front().cost, 2);
    ASSERT_EQ(apply_action(restored, CombatInput{restored.view().boundary, CombatInputKind::end_turn}, scratch).status, StepStatus::awaiting_input);
    EXPECT_EQ(observe(restored).hand_cards.front().cost, 4);
    EXPECT_EQ(observe(state).hand_cards.front().cost, 2);
    ASSERT_EQ(apply_action(state, CombatInput{state.view().boundary, CombatInputKind::play_card, card, true}, scratch).status, StepStatus::awaiting_input);
    EXPECT_EQ(observe(state).combat->energy, 1);
}
TEST(PublicRun, CostModifiersClampAfterAllLayers) {
    const std::array deck{CardSpec{CardId::strike}};
    RunState state;
    ASSERT_EQ(initialize_combat_scenario(state, {}, deck).status, StepStatus::awaiting_input);
    const auto card = observe(state).hand_cards.front().handle;
    auto combat = *state.view().combat;
    combat.cost_modifiers.push_back({card, CostModifierKind::relative, CostExpiry::combat, -3});
    combat.cost_modifiers.push_back({card, CostModifierKind::relative, CostExpiry::combat, 3});
    EXPECT_EQ(observe(state).hand_cards.front().cost, 1);
}
TEST(PublicRun, ReduceOnlyCostExpiresWhenCardIsPlayed) {
    const std::array deck{CardSpec{CardId::strike}};
    RunState state; Scratch scratch;
    ASSERT_EQ(initialize_combat_scenario(state, {}, deck).status, StepStatus::awaiting_input);
    const auto card = observe(state).hand_cards.front().handle;
    auto combat = *state.view().combat;
    combat.cost_modifiers.push_back({card, CostModifierKind::absolute, CostExpiry::turn_or_played, 3, true});
    combat.cost_modifiers.push_back({card, CostModifierKind::relative, CostExpiry::turn_or_played, -1, true});
    EXPECT_EQ(observe(state).hand_cards.front().cost, 0);
    ASSERT_EQ(apply_action(state, CombatInput{state.view().boundary, CombatInputKind::play_card, card, true}, scratch).status, StepStatus::awaiting_input);
    ASSERT_EQ(observe(state).discard.size(), 1u);
    EXPECT_EQ(observe(state).discard.front().cost, 1);
    ASSERT_EQ(apply_action(state, CombatInput{state.view().boundary, CombatInputKind::end_turn}, scratch).status, StepStatus::awaiting_input);
    EXPECT_EQ(observe(state).hand_cards.front().cost, 1);
}
TEST(PublicRun, PlayedCostExpiresAfterSuspendedCardResumes) {
    const std::array deck{CardSpec{CardId::survivor}, CardSpec{CardId::strike}, CardSpec{CardId::defend}};
    RunState state; Scratch scratch;
    ASSERT_EQ(initialize_combat_scenario(state, {}, deck).status, StepStatus::awaiting_input);
    const auto hand = observe(state).hand_cards;
    const auto survivor = std::find_if(hand.begin(), hand.end(), [](const auto& c) { return c.model_id == "CARD.SURVIVOR"; });
    ASSERT_NE(survivor, hand.end());
    state.view().combat->cost_modifiers.push_back({survivor->handle, CostModifierKind::absolute, CostExpiry::played, 0});
    ASSERT_EQ(apply_action(state, CombatInput{state.view().boundary, CombatInputKind::play_card, survivor->handle, false}, scratch).status, StepStatus::awaiting_input);
    ASSERT_EQ(state.view().combat->phase, CombatPhase::discard_choice);
    EXPECT_EQ(state.view().combat->cost_modifiers.size(), 1u);
    RunState restored;
    ASSERT_EQ(restore(restored, checkpoint(state).bytes, scratch), RestoreStatus::restored);
    const auto discarded = observe(restored).hand_cards.front().handle;
    ASSERT_EQ(apply_action(restored, CombatInput{restored.view().boundary, CombatInputKind::select_card, discarded, false}, scratch).status, StepStatus::awaiting_input);
    EXPECT_TRUE(restored.view().combat->cost_modifiers.empty());
    EXPECT_EQ(state.view().combat->cost_modifiers.size(), 1u);
}
TEST(PublicRun, CostLayerOverflowClonesIndependently) {
    const std::array deck{CardSpec{CardId::strike}};
    RunState state; Scratch scratch;
    ASSERT_EQ(initialize_combat_scenario(state, {}, deck).status, StepStatus::awaiting_input);
    const auto card = observe(state).hand_cards.front().handle;
    for (int i = 0; i < 12; ++i)
        state.view().combat->cost_modifiers.push_back({card, CostModifierKind::relative, CostExpiry::turn, 1});
    ASSERT_EQ(observe(state).hand_cards.front().cost, 13);
    RunState copy = state, restored;
    ASSERT_EQ(restore(restored, checkpoint(state).bytes, scratch), RestoreStatus::restored);
    ASSERT_EQ(apply_action(copy, CombatInput{copy.view().boundary, CombatInputKind::end_turn}, scratch).status, StepStatus::awaiting_input);
    ASSERT_EQ(apply_action(restored, CombatInput{restored.view().boundary, CombatInputKind::end_turn}, scratch).status, StepStatus::awaiting_input);
    EXPECT_EQ(observe(copy).hand_cards.front().cost, 1);
    EXPECT_EQ(restored, copy);
    EXPECT_EQ(observe(state).hand_cards.front().cost, 13);
}
TEST(PublicRun, CombatUpgradeDoesNotChangePersistentDeck) {
    const std::array deck{CardSpec{CardId::armaments}, CardSpec{CardId::strike}, CardSpec{CardId::defend}};
    RunState state; Scratch scratch;
    ASSERT_EQ(initialize_combat_scenario(state, {}, deck).status, StepStatus::awaiting_input);
    const auto hand = observe(state).hand_cards;
    const auto source = std::find_if(hand.begin(), hand.end(), [](const auto& c) { return c.model_id == "CARD.ARMAMENTS"; });
    const auto target = std::find_if(hand.begin(), hand.end(), [](const auto& c) { return c.model_id == "CARD.STRIKE_IRONCLAD"; });
    ASSERT_NE(source, hand.end()); ASSERT_NE(target, hand.end());
    ASSERT_EQ(apply_action(state, CombatInput{state.view().boundary, CombatInputKind::play_card, source->handle}, scratch).status, StepStatus::awaiting_input);
    const auto prompt = state.view().combat->selection.id;
    ASSERT_EQ(apply_action(state, CombatInput{state.view().boundary, CombatInputKind::select_card, target->handle, false, prompt}, scratch).status, StepStatus::awaiting_input);
    ASSERT_EQ(apply_action(state, CombatInput{state.view().boundary, CombatInputKind::confirm_selection, {}, false, prompt}, scratch).status, StepStatus::awaiting_input);
    const auto after = observe(state);
    const auto changed = std::find_if(after.hand_cards.begin(), after.hand_cards.end(), [](const auto& c) { return c.model_id == "CARD.STRIKE_IRONCLAD"; });
    ASSERT_NE(changed, after.hand_cards.end()); EXPECT_TRUE(changed->upgraded);
    EXPECT_EQ(after.deck[1].upgrade, 0u);
    RunState restored;
    ASSERT_EQ(restore(restored, checkpoint(state).bytes, scratch), RestoreStatus::restored);
    EXPECT_EQ(restored, state);
}
TEST(PublicRun, PermanentUpgradeTargetsOriginAcrossDeckReorderingAndBranches) {
    const std::array deck{CardSpec{CardId::strike}, CardSpec{CardId::defend}};
    RunState state; Scratch scratch;
    ASSERT_EQ(initialize_combat_scenario(state, {}, deck).status, StepStatus::awaiting_input);
    const auto hand = observe(state).hand_cards;
    const auto strike = std::find_if(hand.begin(), hand.end(), [](const auto& c) { return c.model_id == "CARD.STRIKE_IRONCLAD"; });
    ASSERT_NE(strike, hand.end());
    std::swap(state.view().deck.handles()[0], state.view().deck.handles()[1]);
    RunState original = state, restored;
    ASSERT_EQ(restore(restored, checkpoint(state).bytes, scratch), RestoreStatus::restored);
    ASSERT_EQ(detail::upgrade_card_permanently(restored, strike->handle), Restriction::none);
    ASSERT_EQ(detail::upgrade_card_permanently(restored, strike->handle), Restriction::none);
    EXPECT_EQ(observe(restored).deck[1].upgrade, 1u);
    EXPECT_EQ(observe(restored).deck[0].upgrade, 0u);
    EXPECT_EQ(observe(state).deck[1].upgrade, 0u);
    EXPECT_EQ(state, original);
    EXPECT_TRUE(valid_run(restored));
    RunState clone = restored, reloaded;
    ASSERT_EQ(restore(reloaded, checkpoint(restored).bytes, scratch), RestoreStatus::restored);
    ASSERT_EQ(apply_action(clone, CombatInput{clone.view().boundary, CombatInputKind::play_card, strike->handle, true}, scratch).status, StepStatus::awaiting_input);
    ASSERT_EQ(apply_action(reloaded, CombatInput{reloaded.view().boundary, CombatInputKind::play_card, strike->handle, true}, scratch).status, StepStatus::awaiting_input);
    EXPECT_EQ(clone, reloaded);
    ASSERT_EQ(apply_action(restored, CombatInput{restored.view().boundary, CombatInputKind::end_turn}, scratch).status, StepStatus::awaiting_input);
    EXPECT_NE(clone, restored);
    EXPECT_EQ(state, original);
}
TEST(PublicRun, RemovedPersistentSlotStaysStaleAfterCheckpointRestore) {
    RunState state; Scratch scratch;
    ASSERT_EQ(initialize_run(state, {}).status, StepStatus::awaiting_input);
    auto deck = state.view().deck;
    const auto old = deck.handles()[0];
    const auto card = deck[0];
    ASSERT_TRUE(deck.erase(old));
    ASSERT_TRUE(valid_run(state));
    RunState restored;
    ASSERT_EQ(restore(restored, checkpoint(state).bytes, scratch), RestoreStatus::restored);
    restored.view().deck.push_back(card);
    const auto replacement = restored.view().deck.handles().back();
    EXPECT_EQ(replacement.slot, old.slot);
    EXPECT_NE(replacement.generation, old.generation);
    EXPECT_EQ(restored.view().deck.get(old), nullptr);
    EXPECT_TRUE(valid_run(restored));
    EXPECT_EQ(state.view().deck.get(old), nullptr);
}
TEST(PublicRun, RemovedPersistentCardHandleRejectsReusedSlot) {
    const std::array deck{CardSpec{CardId::strike}, CardSpec{CardId::defend}};
    RunState state;
    ASSERT_EQ(initialize_combat_scenario(state, {}, deck).status, StepStatus::awaiting_input);
    auto cards = state.view().deck;
    const auto old = cards.handles()[0];
    const auto removed = cards[0];
    ASSERT_TRUE(cards.erase(old));
    cards.push_back(removed);
    EXPECT_EQ(cards.handles().back().slot, old.slot);
    EXPECT_NE(cards.handles().back().generation, old.generation);
    EXPECT_EQ(cards.get(old), nullptr);
    EXPECT_FALSE(valid_run(state));
}
TEST(PublicRun, ExhaustRelicCounterSurvivesCombatCleanupAndRestoredNextEncounter) {
    const std::array deck{CardSpec{CardId::tremble}};
    const std::array relics{SetupRules::find("RELIC.CHARONS_ASHES"), SetupRules::find("RELIC.JOSS_PAPER")};
    RunState state; Scratch scratch;
    ASSERT_EQ(initialize_combat_scenario(state, {}, deck, {}, relics, 3).status, StepStatus::awaiting_input);
    const auto source = observe(state).combat->hand.front();
    ASSERT_EQ(apply_action(state, CombatInput{state.view().boundary, CombatInputKind::play_card, source, true}, scratch).status, StepStatus::awaiting_input);
    EXPECT_FALSE(state.view().combat);
    ASSERT_EQ(observe(state).relics.size(), 2u);
    EXPECT_EQ(observe(state).relics[1].counter, 1u);
    RunState restored; ASSERT_EQ(restore(restored, checkpoint(state).bytes, scratch), RestoreStatus::restored);
    for (auto* branch : {&state, &restored}) {
        ASSERT_EQ(apply_action(*branch, RunInput{branch->view().boundary, RunInputKind::skip_rewards}, scratch).status, StepStatus::awaiting_input);
        ASSERT_EQ(enter_combat_scenario(*branch, branch->view().boundary, "ENCOUNTER.SHRINKER_BEETLE_WEAK").status, StepStatus::awaiting_input);
        EXPECT_EQ(observe(*branch).relics[1].counter, 1u);
        EXPECT_EQ(branch->view().combat->joss_paper_counter, 1u);
        EXPECT_FALSE(branch->view().combat->cards.get(source));
    }
    EXPECT_EQ(restored, state);
}
TEST(PublicRun, RestoreRejectsReorderedRunRelicsThatDisagreeWithCapturedCombatInventory) {
    const std::array deck{CardSpec{CardId::purity}, CardSpec{CardId::defend}, CardSpec{CardId::defend}};
    const std::array relics{SetupRules::find("RELIC.CHARONS_ASHES"), SetupRules::find("RELIC.JOSS_PAPER")};
    RunState state; Scratch scratch;
    ASSERT_EQ(initialize_combat_scenario(state, {}, deck, {}, relics).status, StepStatus::awaiting_input);
    const auto before = state;
    std::vector<std::byte> pattern;
    for (const auto value : std::array<std::uint32_t, 3>{2u, relics[0], relics[1]})
        for (unsigned i = 0; i < 4; ++i) pattern.push_back(std::byte(value >> (8 * i)));
    auto bytes = checkpoint(state).bytes;
    const auto at = std::search(bytes.begin(), bytes.end(), pattern.begin(), pattern.end());
    ASSERT_NE(at, bytes.end());
    ASSERT_EQ(std::search(at + pattern.size(), bytes.end(), pattern.begin(), pattern.end()), bytes.end());
    for (unsigned i = 0; i < 4; ++i) std::swap(at[4 + i], at[8 + i]);
    const auto hash = game_hash({reinterpret_cast<const char*>(bytes.data()), bytes.size() - 8});
    for (unsigned i = 0; i < 8; ++i) bytes[bytes.size() - 8 + i] = std::byte(hash >> (8 * i));
    scratch.steps = 73;
    EXPECT_EQ(restore(state, bytes, scratch), RestoreStatus::malformed);
    EXPECT_EQ(state, before); EXPECT_EQ(scratch.steps, 73u);
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
    std::swap(state.view().deck.handles().front(), state.view().deck.handles().back());
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
