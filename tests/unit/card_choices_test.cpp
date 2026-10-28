#include "sts2/engine/action.hpp"
#include "sts2/observation/observe.hpp"
#include "sts2/persistence/checkpoint.hpp"
#include "storage_allocator.hpp"
#include <gtest/gtest.h>
#include <algorithm>

namespace sts2::engine {
namespace {
CardHandle find(const CombatState& state, CardId id) {
    for (auto h : state.zone(Zone::hand)) if (state.view().cards.get(h)->definition == id) return h;
    return {};
}
StepResult play(CombatState& state, CardHandle card, Scratch& scratch) {
    return apply_combat(state, {state.view().boundary, CombatInputKind::play_card, card,
        Ruleset::pinned().card(state.view().cards.get(card)->definition)->enemy_target}, scratch);
}
StepResult choose(CombatState& state, CombatInputKind kind, Scratch& scratch, CardHandle card = {}) {
    return apply_combat(state, {state.view().boundary, kind, card, false, observe(state).selection->id}, scratch);
}
}
TEST(CardChoices, PurityClampsVisibleMaximumToNativeCandidateCount) {
    const std::array deck{CardSpec{CardId::purity, true}, CardSpec{CardId::strike}};
    CombatConfig config; config.deck = deck;
    CombatState state; Scratch scratch;
    ASSERT_EQ(initialize_combat(state, config, scratch).status, StepStatus::awaiting_input);
    ASSERT_EQ(play(state, find(state, CardId::purity), scratch).status, StepStatus::awaiting_input);
    const auto prompt = observe(state).selection;
    ASSERT_TRUE(prompt);
    EXPECT_EQ(prompt->maximum, 1u);
    EXPECT_TRUE(valid_combat(state));
}
TEST(CardChoices, PurityConfirmsOrderedSelectionAndRestoresIndependentFutures) {
    const std::array deck{CardSpec{CardId::purity}, CardSpec{CardId::strike}, CardSpec{CardId::defend}, CardSpec{CardId::bash}};
    CombatConfig config; config.deck = deck;
    CombatState state; Scratch scratch;
    ASSERT_EQ(initialize_combat(state, config, scratch).status, StepStatus::awaiting_input);
    auto source = find(state, CardId::purity);
    ASSERT_EQ(play(state, source, scratch).status, StepStatus::awaiting_input);
    ASSERT_TRUE(observe(state).selection);
    const auto first = state.zone(Zone::hand)[0], second = state.zone(Zone::hand)[1];
    ASSERT_EQ(choose(state, CombatInputKind::select_card, scratch, second).status, StepStatus::awaiting_input);
    ASSERT_EQ(choose(state, CombatInputKind::select_card, scratch, first).status, StepStatus::awaiting_input);
    EXPECT_TRUE(state.zone(Zone::exhaust).empty());
    CombatState restored;
    ASSERT_EQ(restore(restored, checkpoint(state).bytes, scratch), RestoreStatus::restored);
    auto alternative = state;
    ASSERT_EQ(choose(state, CombatInputKind::confirm_selection, scratch).status, StepStatus::awaiting_input);
    ASSERT_EQ(choose(restored, CombatInputKind::confirm_selection, scratch).status, StepStatus::awaiting_input);
    EXPECT_EQ(state, restored);
    ASSERT_EQ(state.zone(Zone::exhaust).size(), 3u);
    EXPECT_EQ(state.zone(Zone::exhaust)[0], second);
    EXPECT_EQ(state.zone(Zone::exhaust)[1], first);
    EXPECT_EQ(state.zone(Zone::exhaust)[2], source);
    ASSERT_EQ(choose(alternative, CombatInputKind::deselect_card, scratch, first).status, StepStatus::awaiting_input);
    ASSERT_EQ(choose(alternative, CombatInputKind::confirm_selection, scratch).status, StepStatus::awaiting_input);
    EXPECT_EQ(alternative.zone(Zone::exhaust).size(), 2u);
    EXPECT_EQ(state.view().energy, 3);
    EXPECT_EQ(state.view().counters.started_plays, 1u);
    EXPECT_TRUE(valid_combat(state));
}
TEST(CardChoices, PublicChoiceActionsExposeSelectedOrderAndRejectWithoutMutation) {
    const std::array deck{CardSpec{CardId::purity}, CardSpec{CardId::strike}, CardSpec{CardId::defend}};
    CombatConfig config; config.deck = deck;
    CombatState state; Scratch scratch;
    ASSERT_EQ(initialize_combat(state, config, scratch).status, StepStatus::awaiting_input);
    const auto source = *std::find_if(state.zone(Zone::hand).begin(), state.zone(Zone::hand).end(), [&](auto h) { return state.view().cards.get(h)->definition == CardId::purity; });
    ASSERT_EQ(apply_combat(state, {state.view().boundary, CombatInputKind::play_card, source}, scratch).status, StepStatus::awaiting_input);
    const auto before = state;
    const auto view = observe(state);
    ASSERT_TRUE(view.selection.has_value());
    EXPECT_EQ(view.selection->id, state.view().selection.id);
    EXPECT_EQ(view.selection->candidates.size(), 2u);
    EXPECT_TRUE(view.selection->can_confirm);
    EXPECT_EQ(ActionSpaceView(state).size(), 3u);
    EXPECT_EQ(apply_combat(state, {state.view().boundary, CombatInputKind::select_card, source, false, state.view().selection.id}, scratch).status, StepStatus::invalid_action);
    EXPECT_EQ(apply_combat(state, {state.view().boundary, CombatInputKind::confirm_selection, {}, false, PromptId{999}}, scratch).status, StepStatus::invalid_action);
    EXPECT_EQ(state, before);
    const auto input = ActionSpaceView(state).at(0)->input;
    ASSERT_EQ(apply_combat(state, input, scratch).status, StepStatus::awaiting_input);
    const auto selected_view = observe(state);
    ASSERT_EQ(selected_view.selection->selected.size(), 1u);
    EXPECT_EQ(selected_view.selection->selected[0], input.card);
    EXPECT_EQ(ActionSpaceView(state).at(0)->input.kind, CombatInputKind::deselect_card);
}

TEST(CardChoices, ArmamentsPaysAndBlocksBeforeChoiceThenUpgradesOnlySelectedCombatCopy) {
    const std::array deck{CardSpec{CardId::armaments}, CardSpec{CardId::strike}, CardSpec{CardId::defend}};
    CombatConfig config; config.deck = deck;
    CombatState state; Scratch scratch;
    ASSERT_EQ(initialize_combat(state, config, scratch).status, StepStatus::awaiting_input);
    const auto source = *std::find_if(state.zone(Zone::hand).begin(), state.zone(Zone::hand).end(), [&](auto h) { return state.view().cards.get(h)->definition == CardId::armaments; });
    ASSERT_EQ(apply_combat(state, {state.view().boundary, CombatInputKind::play_card, source}, scratch).status, StepStatus::awaiting_input);
    ASSERT_EQ(state.view().phase, CombatPhase::card_choice);
    EXPECT_EQ(state.view().player.block, 5); EXPECT_EQ(state.view().energy, 2);
    const auto chosen = state.view().selection_candidates.back();
    ASSERT_EQ(apply_combat(state, {state.view().boundary, CombatInputKind::select_card, chosen, false, state.view().selection.id}, scratch).status, StepStatus::awaiting_input);
    EXPECT_FALSE(state.view().cards.get(chosen)->upgraded);
    CombatState copy; ASSERT_EQ(restore(copy, checkpoint(state).bytes, scratch), RestoreStatus::restored);
    ASSERT_EQ(apply_combat(copy, {copy.view().boundary, CombatInputKind::confirm_selection, {}, false, copy.view().selection.id}, scratch).status, StepStatus::awaiting_input);
    EXPECT_TRUE(copy.view().cards.get(chosen)->upgraded);
    EXPECT_FALSE(state.view().cards.get(chosen)->upgraded);
    EXPECT_EQ(copy.view().energy, 2); EXPECT_EQ(copy.view().player.block, 5);
    EXPECT_TRUE(valid_combat(copy));
}

TEST(CardChoices, HeadbuttSelectsDiscardAndAutomaticallyReturnsItToTop) {
    const std::array deck{CardSpec{CardId::headbutt}, CardSpec{CardId::defend}, CardSpec{CardId::defend}};
    CombatConfig config; config.deck = deck;
    CombatState state; Scratch scratch;
    ASSERT_EQ(initialize_combat(state, config, scratch).status, StepStatus::awaiting_input);
    const auto cards = state.zone(Zone::hand);
    const std::vector<CardHandle> hand(cards.begin(), cards.end());
    CardHandle source;
    for (auto card : hand) {
        if (state.view().cards.get(card)->definition == CardId::headbutt) source = card;
        else ASSERT_EQ(apply_combat(state, {state.view().boundary, CombatInputKind::play_card, card}, scratch).status, StepStatus::awaiting_input);
    }
    ASSERT_EQ(apply_combat(state, {state.view().boundary, CombatInputKind::play_card, source, true}, scratch).status, StepStatus::awaiting_input);
    ASSERT_EQ(state.view().phase, CombatPhase::card_choice);
    const auto chosen = state.view().selection_candidates.back();
    EXPECT_TRUE(state.view().selection.automatic_at_maximum);
    EXPECT_EQ(ActionSpaceView(state).size(), 2u);
    ASSERT_EQ(apply_combat(state, {state.view().boundary, CombatInputKind::select_card, chosen, false, state.view().selection.id}, scratch).status, StepStatus::awaiting_input);
    EXPECT_EQ(state.view().phase, CombatPhase::play);
    ASSERT_FALSE(state.zone(Zone::draw).empty());
    EXPECT_EQ(state.zone(Zone::draw).front(), chosen);
    EXPECT_EQ(state.zone(Zone::discard).back(), source);
    EXPECT_TRUE(valid_combat(state));
}

TEST(CardChoices, BurningPactDrawsOnceAfterConfirmedExhaustAndSurvivesPartialRestore) {
    std::array<CardSpec, 8> deck; deck.fill({CardId::burning_pact, true});
    CombatConfig config; config.deck = deck;
    CombatState state; Scratch scratch;
    ASSERT_EQ(initialize_combat(state, config, scratch).status, StepStatus::awaiting_input);
    const auto source = *std::find_if(state.zone(Zone::hand).begin(), state.zone(Zone::hand).end(), [&](auto h) { return state.view().cards.get(h)->definition == CardId::burning_pact; });
    ASSERT_EQ(apply_combat(state, {state.view().boundary, CombatInputKind::play_card, source}, scratch).status, StepStatus::awaiting_input);
    const auto chosen = state.view().selection_candidates.front();
    ASSERT_EQ(apply_combat(state, {state.view().boundary, CombatInputKind::select_card, chosen, false, state.view().selection.id}, scratch).status, StepStatus::awaiting_input);
    EXPECT_EQ(state.zone(Zone::hand).size(), 4u);
    CombatState copy; ASSERT_EQ(restore(copy, checkpoint(state).bytes, scratch), RestoreStatus::restored);
    const auto confirm = CombatInput{copy.view().boundary, CombatInputKind::confirm_selection, {}, false, copy.view().selection.id};
    ASSERT_EQ(apply_combat(copy, confirm, scratch).status, StepStatus::awaiting_input);
    EXPECT_EQ(copy.zone(Zone::exhaust), (std::vector<CardHandle>{chosen}));
     EXPECT_EQ(copy.zone(Zone::hand).size(), 6u); EXPECT_EQ(copy.view().counters.non_hand_draws_this_turn, 3u);
    EXPECT_EQ(copy.view().counters.started_plays, 1u); EXPECT_EQ(copy.view().energy, 2);
    EXPECT_EQ(copy.view().phase, CombatPhase::play);
    const auto before = copy;
    EXPECT_EQ(apply_combat(copy, confirm, scratch).status, StepStatus::invalid_action);
    EXPECT_EQ(copy, before);
}

TEST(CardChoices, TrueGritUpgradedChoosesAfterBlockAndBaseUsesOwnedSelectionRng) {
    for (const bool upgraded : {false, true}) {
        const std::array deck{CardSpec{CardId::true_grit, upgraded}, CardSpec{CardId::strike}, CardSpec{CardId::defend}};
        CombatConfig config; config.deck = deck;
        CombatState state; Scratch scratch;
        ASSERT_EQ(initialize_combat(state, config, scratch).status, StepStatus::awaiting_input);
        const auto source = *std::find_if(state.zone(Zone::hand).begin(), state.zone(Zone::hand).end(), [&](auto h) { return state.view().cards.get(h)->definition == CardId::true_grit; });
        const auto rng = state.view().rng.get(RunStream::combat_card_selection).state();
        ASSERT_EQ(apply_combat(state, {state.view().boundary, CombatInputKind::play_card, source}, scratch).status, StepStatus::awaiting_input);
        EXPECT_EQ(state.view().player.block, upgraded ? 9 : 7);
        if (upgraded) {
            ASSERT_EQ(state.view().phase, CombatPhase::card_choice);
            const auto chosen = state.view().selection_candidates.back();
            ASSERT_EQ(apply_combat(state, {state.view().boundary, CombatInputKind::select_card, chosen, false, state.view().selection.id}, scratch).status, StepStatus::awaiting_input);
            ASSERT_EQ(apply_combat(state, {state.view().boundary, CombatInputKind::confirm_selection, {}, false, state.view().selection.id}, scratch).status, StepStatus::awaiting_input);
            EXPECT_EQ(state.zone(Zone::exhaust), (std::vector<CardHandle>{chosen}));
            EXPECT_EQ(state.view().rng.get(RunStream::combat_card_selection).state(), rng);
        } else {
            EXPECT_EQ(state.view().phase, CombatPhase::play);
            EXPECT_EQ(state.view().rng.get(RunStream::combat_card_selection).state().counter, rng.counter + 1);
            EXPECT_EQ(state.zone(Zone::exhaust).size(), 1u);
        }
        EXPECT_EQ(state.view().counters.started_plays, 1u); EXPECT_EQ(state.view().energy, 2);
        EXPECT_TRUE(valid_combat(state));
    }
}

TEST(CardChoices, SelectionAtCapacityReplacesLastAndDeselectReselectAppends) {
    const std::array deck{CardSpec{CardId::purity}, CardSpec{CardId::strike}, CardSpec{CardId::strike}, CardSpec{CardId::defend}, CardSpec{CardId::bash}};
    CombatConfig config; config.deck = deck; CombatState state; Scratch scratch;
    ASSERT_EQ(initialize_combat(state, config, scratch).status, StepStatus::awaiting_input);
    ASSERT_EQ(play(state, find(state, CardId::purity), scratch).status, StepStatus::awaiting_input);
    const auto offered = observe(state).selection->candidates;
    std::array<CardHandle, 4> cards; std::copy(offered.begin(), offered.end(), cards.begin());
    for (auto card : cards) ASSERT_EQ(choose(state, CombatInputKind::select_card, scratch, card).status, StepStatus::awaiting_input);
    auto selected = observe(state).selection->selected;
    ASSERT_EQ(selected.size(), 3u);
    EXPECT_EQ(selected[0], cards[0]); EXPECT_EQ(selected[1], cards[1]); EXPECT_EQ(selected[2], cards[3]);
    ASSERT_EQ(choose(state, CombatInputKind::deselect_card, scratch, cards[0]).status, StepStatus::awaiting_input);
    ASSERT_EQ(choose(state, CombatInputKind::select_card, scratch, cards[0]).status, StepStatus::awaiting_input);
    selected = observe(state).selection->selected;
    EXPECT_EQ(selected[0], cards[1]); EXPECT_EQ(selected[1], cards[3]); EXPECT_EQ(selected[2], cards[0]);
    EXPECT_TRUE(valid_combat(state));
}
TEST(CardChoices, DiscardPromptOverflowCopiesRemainIndependent) {
    std::array<CardSpec, 40> deck; deck.fill({CardId::headbutt});
    CombatConfig config; config.deck = deck; CombatState state; Scratch scratch;
    ASSERT_EQ(initialize_combat(state, config, scratch).status, StepStatus::awaiting_input);
    for (int i = 0; i < 3; ++i) ASSERT_EQ(apply_combat(state, {state.view().boundary, CombatInputKind::end_turn}, scratch).status, StepStatus::awaiting_input);
    ASSERT_EQ(play(state, find(state, CardId::headbutt), scratch).status, StepStatus::awaiting_input);
    ASSERT_EQ(observe(state).selection->candidates.size(), 15u);
    auto cloned = state; CombatState restored;
    ASSERT_EQ(restore(restored, checkpoint(state).bytes, scratch), RestoreStatus::restored);
    const auto first = observe(state).selection->candidates.front(), last = observe(state).selection->candidates.back();
    ASSERT_EQ(choose(state, CombatInputKind::select_card, scratch, first).status, StepStatus::awaiting_input);
    ASSERT_EQ(choose(restored, CombatInputKind::select_card, scratch, first).status, StepStatus::awaiting_input);
    EXPECT_EQ(state, restored);
    ASSERT_EQ(choose(cloned, CombatInputKind::select_card, scratch, last).status, StepStatus::awaiting_input);
    EXPECT_EQ(state.zone(Zone::draw).front(), first); EXPECT_EQ(cloned.zone(Zone::draw).front(), last);
    EXPECT_TRUE(valid_combat(state)); EXPECT_TRUE(valid_combat(cloned));
}
TEST(CardChoices, EmptyAndForcedSelectionsResolveWithoutPublishingPrompts) {
    for (auto id : {CardId::purity, CardId::armaments, CardId::headbutt, CardId::burning_pact, CardId::true_grit}) {
        for (bool upgraded : {false, true}) {
            const std::array deck{CardSpec{id, upgraded}};
            CombatConfig config; config.deck = deck; CombatState state; Scratch scratch;
            ASSERT_EQ(initialize_combat(state, config, scratch).status, StepStatus::awaiting_input);
            ASSERT_EQ(play(state, state.zone(Zone::hand).front(), scratch).status, StepStatus::awaiting_input);
            EXPECT_FALSE(observe(state).selection); EXPECT_TRUE(valid_combat(state));
        }
    }
    for (auto id : {CardId::armaments, CardId::burning_pact, CardId::true_grit}) {
        const std::array deck{CardSpec{id, true}, CardSpec{CardId::strike}};
        CombatConfig config; config.deck = deck; CombatState state; Scratch scratch;
        ASSERT_EQ(initialize_combat(state, config, scratch).status, StepStatus::awaiting_input);
        ASSERT_EQ(play(state, find(state, id), scratch).status, StepStatus::awaiting_input);
        EXPECT_FALSE(observe(state).selection); EXPECT_TRUE(valid_combat(state));
    }
}
TEST(CardChoices, PurityRetainsAcrossTurnsAndEveryPartialInputAdvancesClockOnce) {
    const std::array deck{CardSpec{CardId::purity}, CardSpec{CardId::defend}};
    CombatConfig config; config.deck = deck; config.clock = {1234, 7}; CombatState state; Scratch scratch;
    ASSERT_EQ(initialize_combat(state, config, scratch).status, StepStatus::awaiting_input);
    const auto source = find(state, CardId::purity);
    ASSERT_EQ(apply_combat(state, {state.view().boundary, CombatInputKind::end_turn}, scratch).status, StepStatus::awaiting_input);
    EXPECT_EQ(find(state, CardId::purity), source);
    ASSERT_EQ(play(state, source, scratch).status, StepStatus::awaiting_input);
    const auto chosen = observe(state).selection->candidates.front();
    ASSERT_EQ(choose(state, CombatInputKind::select_card, scratch, chosen).status, StepStatus::awaiting_input);
    EXPECT_EQ(state.view().clock.elapsed_seconds(), 21);
    const auto before = checkpoint(state).bytes;
    (void)observe(state); (void)ActionSpaceView(state).at(0);
    EXPECT_EQ(choose(state, CombatInputKind::select_card, scratch, chosen).status, StepStatus::invalid_action);
    EXPECT_EQ(checkpoint(state).bytes, before);
    ASSERT_EQ(choose(state, CombatInputKind::deselect_card, scratch, chosen).status, StepStatus::awaiting_input);
    ASSERT_EQ(choose(state, CombatInputKind::confirm_selection, scratch).status, StepStatus::awaiting_input);
    EXPECT_EQ(state.view().clock.elapsed_seconds(), 35); EXPECT_EQ(state.view().boundary.value, 6u);
    EXPECT_TRUE(valid_combat(state));
}
TEST(CardChoices, CorruptPartialReferencesAreRejectedAtomically) {
    const std::array deck{CardSpec{CardId::purity}, CardSpec{CardId::strike}, CardSpec{CardId::defend}};
    CombatConfig config; config.deck = deck; CombatState state; Scratch scratch;
    ASSERT_EQ(initialize_combat(state, config, scratch).status, StepStatus::awaiting_input);
    ASSERT_EQ(play(state, find(state, CardId::purity), scratch).status, StepStatus::awaiting_input);
    ASSERT_EQ(choose(state, CombatInputKind::select_card, scratch, state.zone(Zone::hand).front()).status, StepStatus::awaiting_input);
    auto bytes = checkpoint(state).bytes;
    // The selected handle immediately precedes the seven counters and power ID.
    const auto generation_at = bytes.size() - 8 - 32 - 4;
    for (int i = 0; i < 4; ++i) bytes[generation_at + i] = std::byte{0};
    const auto hash = game_hash({reinterpret_cast<const char*>(bytes.data()), bytes.size() - 8});
    for (int i = 0; i < 8; ++i) bytes[bytes.size() - 8 + i] = std::byte(static_cast<std::uint8_t>(hash >> (8 * i)));
    const auto before = checkpoint(state).bytes;
    EXPECT_EQ(restore(state, bytes, scratch), RestoreStatus::malformed);
    EXPECT_EQ(checkpoint(state).bytes, before);
}
TEST(CardChoices, CommonPartialPromptClonesUseOnlyByteCopyStorage) {
    test::AllocationCounter allocation;
    const std::array deck{CardSpec{CardId::purity}, CardSpec{CardId::strike}, CardSpec{CardId::defend}};
    CombatConfig config; config.deck = deck; CombatState state(allocation.allocator()), cloned(allocation.allocator()); Scratch scratch;
    ASSERT_EQ(initialize_combat(state, config, scratch).status, StepStatus::awaiting_input);
    ASSERT_EQ(play(state, find(state, CardId::purity), scratch).status, StepStatus::awaiting_input);
    ASSERT_EQ(choose(state, CombatInputKind::select_card, scratch, state.zone(Zone::hand).front()).status, StepStatus::awaiting_input);
    ASSERT_TRUE(state.overflow.empty());
    const auto count = allocation.allocations; allocation.fail_after = count;
    ASSERT_EQ(clone_into(cloned, state), CloneStatus::cloned);
    EXPECT_EQ(allocation.allocations, count); EXPECT_EQ(cloned, state);
    CombatState relocated(allocation.allocator());
    std::memcpy(&relocated.data, &state.data, sizeof(CombatData));
    EXPECT_TRUE(valid_combat(relocated)); EXPECT_EQ(relocated, state);
    ASSERT_EQ(choose(cloned, CombatInputKind::confirm_selection, scratch).status, StepStatus::awaiting_input);
    EXPECT_TRUE(observe(state).selection); EXPECT_FALSE(observe(cloned).selection);
    EXPECT_EQ(allocation.allocations, count);
}
} // namespace sts2::engine
