#include "sts2/kernel/simulator.hpp"
#include "sts2/observation/observe.hpp"
#include "sts2/persistence/checkpoint.hpp"
#include <gtest/gtest.h>
#include <algorithm>

namespace sts2::engine {
TEST(CardChoices, PurityRequiresConfirmationAndExhaustsInSelectionOrder) {
    const std::array deck{CardSpec{CardId::purity}, CardSpec{CardId::strike}, CardSpec{CardId::defend}, CardSpec{CardId::bash}};
    CombatConfig config; config.deck = deck;
    State state; Scratch scratch; Simulator simulator;
    ASSERT_EQ(simulator.initialize(state, config, scratch).status, StepStatus::awaiting_input);
    const auto purity = *std::find_if(state.zone(Zone::hand).begin(), state.zone(Zone::hand).end(), [&](auto h) { return state.cards.get(h)->definition == CardId::purity; });
    ASSERT_EQ(simulator.apply(state, {state.boundary, InputKind::play_card, purity}, scratch).status, StepStatus::awaiting_input);
    ASSERT_EQ(state.phase, Phase::card_choice);
    const auto first = state.zone(Zone::hand)[0], second = state.zone(Zone::hand)[1];
    ASSERT_EQ(simulator.apply(state, {state.boundary, InputKind::select_card, second, false, state.selection.id}, scratch).status, StepStatus::awaiting_input);
    ASSERT_EQ(simulator.apply(state, {state.boundary, InputKind::select_card, first, false, state.selection.id}, scratch).status, StepStatus::awaiting_input);
    EXPECT_TRUE(state.zone(Zone::exhaust).empty());
    ASSERT_EQ(simulator.apply(state, {state.boundary, InputKind::confirm_selection, {}, false, state.selection.id}, scratch).status, StepStatus::awaiting_input);
    EXPECT_EQ(state.zone(Zone::exhaust), (std::vector<CardHandle>{second, first, purity}));
    EXPECT_EQ(state.phase, Phase::play);
    EXPECT_EQ(state.cards_played, 1u);
    EXPECT_TRUE(valid_state(state));
}
TEST(CardChoices, PartialPurityCheckpointOwnsOrderedSelectionsAndIndependentFutures) {
    const std::array deck{CardSpec{CardId::purity}, CardSpec{CardId::strike}, CardSpec{CardId::defend}, CardSpec{CardId::bash}, CardSpec{CardId::defend}};
    CombatConfig config; config.deck = deck;
    State state; Scratch scratch; Simulator simulator;
    ASSERT_EQ(simulator.initialize(state, config, scratch).status, StepStatus::awaiting_input);
    const auto source = *std::find_if(state.zone(Zone::hand).begin(), state.zone(Zone::hand).end(), [&](auto h) { return state.cards.get(h)->definition == CardId::purity; });
    ASSERT_EQ(simulator.apply(state, {state.boundary, InputKind::play_card, source}, scratch).status, StepStatus::awaiting_input);
    const auto candidates = state.selection.candidates;
    const auto choose = [&](State& target, InputKind kind, CardHandle card = {}) {
        return simulator.apply(target, {target.boundary, kind, card, false, target.selection.id}, scratch);
    };
    for (auto card : candidates) ASSERT_EQ(choose(state, InputKind::select_card, card).status, StepStatus::awaiting_input);
    EXPECT_EQ(state.selection.selected, (std::vector<CardHandle>{candidates[0], candidates[1], candidates[3]}));
    ASSERT_EQ(choose(state, InputKind::deselect_card, candidates[0]).status, StepStatus::awaiting_input);
    ASSERT_EQ(choose(state, InputKind::select_card, candidates[0]).status, StepStatus::awaiting_input);
    State restored; ASSERT_EQ(restore(restored, checkpoint(state).bytes, scratch), RestoreStatus::restored);
    EXPECT_EQ(restored, state);
    State clone; clone_into(clone, state);
    ASSERT_EQ(choose(state, InputKind::confirm_selection).status, StepStatus::awaiting_input);
    ASSERT_EQ(choose(restored, InputKind::confirm_selection).status, StepStatus::awaiting_input);
    EXPECT_EQ(restored, state);
    ASSERT_EQ(choose(clone, InputKind::deselect_card, candidates[0]).status, StepStatus::awaiting_input);
    ASSERT_EQ(choose(clone, InputKind::confirm_selection).status, StepStatus::awaiting_input);
    EXPECT_NE(clone, state);
    EXPECT_EQ(clone.cards_played, 1u);
    EXPECT_EQ(clone.energy, 3);
}

TEST(CardChoices, PublicChoiceActionsExposeSelectedOrderAndRejectWithoutMutation) {
    const std::array deck{CardSpec{CardId::purity}, CardSpec{CardId::strike}, CardSpec{CardId::defend}};
    CombatConfig config; config.deck = deck;
    State state; Scratch scratch; Simulator simulator;
    ASSERT_EQ(simulator.initialize(state, config, scratch).status, StepStatus::awaiting_input);
    const auto source = *std::find_if(state.zone(Zone::hand).begin(), state.zone(Zone::hand).end(), [&](auto h) { return state.cards.get(h)->definition == CardId::purity; });
    ASSERT_EQ(simulator.apply(state, {state.boundary, InputKind::play_card, source}, scratch).status, StepStatus::awaiting_input);
    const auto before = state;
    const auto view = observe(state);
    ASSERT_TRUE(view.selection.has_value());
    EXPECT_EQ(view.selection->id, state.selection.id);
    EXPECT_EQ(view.selection->candidates.size(), 2u);
    EXPECT_TRUE(view.selection->can_confirm);
    EXPECT_EQ(ActionSpaceView(state).size(), 3u);
    EXPECT_EQ(simulator.apply(state, {state.boundary, InputKind::select_card, source, false, state.selection.id}, scratch).status, StepStatus::invalid_action);
    EXPECT_EQ(simulator.apply(state, {state.boundary, InputKind::confirm_selection, {}, false, PromptId{999}}, scratch).status, StepStatus::invalid_action);
    EXPECT_EQ(state, before);
    const auto input = ActionSpaceView(state).at(0)->input;
    ASSERT_EQ(simulator.apply(state, input, scratch).status, StepStatus::awaiting_input);
    const auto selected_view = observe(state);
    ASSERT_EQ(selected_view.selection->selected.size(), 1u);
    EXPECT_EQ(selected_view.selection->selected[0], input.card);
    EXPECT_EQ(ActionSpaceView(state).at(0)->input.kind, InputKind::deselect_card);
}

TEST(CardChoices, ArmamentsPaysAndBlocksBeforeChoiceThenUpgradesOnlySelectedCombatCopy) {
    const std::array deck{CardSpec{CardId::armaments}, CardSpec{CardId::strike}, CardSpec{CardId::defend}};
    CombatConfig config; config.deck = deck;
    State state; Scratch scratch; Simulator simulator;
    ASSERT_EQ(simulator.initialize(state, config, scratch).status, StepStatus::awaiting_input);
    const auto source = *std::find_if(state.zone(Zone::hand).begin(), state.zone(Zone::hand).end(), [&](auto h) { return state.cards.get(h)->definition == CardId::armaments; });
    ASSERT_EQ(simulator.apply(state, {state.boundary, InputKind::play_card, source}, scratch).status, StepStatus::awaiting_input);
    ASSERT_EQ(state.phase, Phase::card_choice);
    EXPECT_EQ(state.player.block, 5); EXPECT_EQ(state.energy, 2);
    const auto chosen = state.selection.candidates.back();
    ASSERT_EQ(simulator.apply(state, {state.boundary, InputKind::select_card, chosen, false, state.selection.id}, scratch).status, StepStatus::awaiting_input);
    EXPECT_FALSE(state.cards.get(chosen)->upgraded);
    State copy; ASSERT_EQ(restore(copy, checkpoint(state).bytes, scratch), RestoreStatus::restored);
    ASSERT_EQ(simulator.apply(copy, {copy.boundary, InputKind::confirm_selection, {}, false, copy.selection.id}, scratch).status, StepStatus::awaiting_input);
    EXPECT_TRUE(copy.cards.get(chosen)->upgraded);
    EXPECT_FALSE(state.cards.get(chosen)->upgraded);
    EXPECT_EQ(copy.energy, 2); EXPECT_EQ(copy.player.block, 5);
    EXPECT_TRUE(valid_state(copy));
}

TEST(CardChoices, HeadbuttSelectsDiscardAndAutomaticallyReturnsItToTop) {
    const std::array deck{CardSpec{CardId::headbutt}, CardSpec{CardId::defend}, CardSpec{CardId::defend}};
    CombatConfig config; config.deck = deck;
    State state; Scratch scratch; Simulator simulator;
    ASSERT_EQ(simulator.initialize(state, config, scratch).status, StepStatus::awaiting_input);
    const auto hand = state.zone(Zone::hand);
    CardHandle source;
    for (auto card : hand) {
        if (state.cards.get(card)->definition == CardId::headbutt) source = card;
        else ASSERT_EQ(simulator.apply(state, {state.boundary, InputKind::play_card, card}, scratch).status, StepStatus::awaiting_input);
    }
    ASSERT_EQ(simulator.apply(state, {state.boundary, InputKind::play_card, source, true}, scratch).status, StepStatus::awaiting_input);
    ASSERT_EQ(state.phase, Phase::card_choice);
    const auto chosen = state.selection.candidates.back();
    EXPECT_TRUE(state.selection.automatic_at_maximum);
    ASSERT_EQ(simulator.apply(state, {state.boundary, InputKind::select_card, chosen, false, state.selection.id}, scratch).status, StepStatus::awaiting_input);
    EXPECT_EQ(state.phase, Phase::play);
    ASSERT_FALSE(state.zone(Zone::draw).empty());
    EXPECT_EQ(state.zone(Zone::draw).front(), chosen);
    EXPECT_EQ(state.zone(Zone::discard).back(), source);
    EXPECT_TRUE(valid_state(state));
}

TEST(CardChoices, BurningPactDrawsOnceAfterConfirmedExhaustAndSurvivesPartialRestore) {
    std::array<CardSpec, 8> deck; deck.fill({CardId::burning_pact, true});
    CombatConfig config; config.deck = deck;
    State state; Scratch scratch; Simulator simulator;
    ASSERT_EQ(simulator.initialize(state, config, scratch).status, StepStatus::awaiting_input);
    const auto source = *std::find_if(state.zone(Zone::hand).begin(), state.zone(Zone::hand).end(), [&](auto h) { return state.cards.get(h)->definition == CardId::burning_pact; });
    ASSERT_EQ(simulator.apply(state, {state.boundary, InputKind::play_card, source}, scratch).status, StepStatus::awaiting_input);
    const auto chosen = state.selection.candidates.front();
    ASSERT_EQ(simulator.apply(state, {state.boundary, InputKind::select_card, chosen, false, state.selection.id}, scratch).status, StepStatus::awaiting_input);
    EXPECT_EQ(state.cards_drawn, 5u);
    State copy; ASSERT_EQ(restore(copy, checkpoint(state).bytes, scratch), RestoreStatus::restored);
    const auto confirm = Input{copy.boundary, InputKind::confirm_selection, {}, false, copy.selection.id};
    ASSERT_EQ(simulator.apply(copy, confirm, scratch).status, StepStatus::awaiting_input);
    EXPECT_EQ(copy.zone(Zone::exhaust), (std::vector<CardHandle>{chosen}));
    EXPECT_EQ(copy.cards_drawn, 8u); EXPECT_EQ(copy.zone(Zone::hand).size(), 6u);
    EXPECT_EQ(copy.cards_played, 1u); EXPECT_EQ(copy.energy, 2);
    EXPECT_EQ(copy.phase, Phase::play);
    const auto before = copy;
    EXPECT_EQ(simulator.apply(copy, confirm, scratch).status, StepStatus::invalid_action);
    EXPECT_EQ(copy, before);
}

TEST(CardChoices, TrueGritUpgradedChoosesAfterBlockAndBaseUsesOwnedSelectionRng) {
    for (const bool upgraded : {false, true}) {
        const std::array deck{CardSpec{CardId::true_grit, upgraded}, CardSpec{CardId::strike}, CardSpec{CardId::defend}};
        CombatConfig config; config.deck = deck;
        State state; Scratch scratch; Simulator simulator;
        ASSERT_EQ(simulator.initialize(state, config, scratch).status, StepStatus::awaiting_input);
        const auto source = *std::find_if(state.zone(Zone::hand).begin(), state.zone(Zone::hand).end(), [&](auto h) { return state.cards.get(h)->definition == CardId::true_grit; });
        const auto rng = state.rng.get(RunStream::combat_card_selection).state();
        ASSERT_EQ(simulator.apply(state, {state.boundary, InputKind::play_card, source}, scratch).status, StepStatus::awaiting_input);
        EXPECT_EQ(state.player.block, upgraded ? 9 : 7);
        if (upgraded) {
            ASSERT_EQ(state.phase, Phase::card_choice);
            const auto chosen = state.selection.candidates.back();
            ASSERT_EQ(simulator.apply(state, {state.boundary, InputKind::select_card, chosen, false, state.selection.id}, scratch).status, StepStatus::awaiting_input);
            ASSERT_EQ(simulator.apply(state, {state.boundary, InputKind::confirm_selection, {}, false, state.selection.id}, scratch).status, StepStatus::awaiting_input);
            EXPECT_EQ(state.zone(Zone::exhaust), (std::vector<CardHandle>{chosen}));
            EXPECT_EQ(state.rng.get(RunStream::combat_card_selection).state(), rng);
        } else {
            EXPECT_EQ(state.phase, Phase::play);
            EXPECT_EQ(state.rng.get(RunStream::combat_card_selection).state().counter, rng.counter + 1);
            EXPECT_EQ(state.zone(Zone::exhaust).size(), 1u);
        }
        EXPECT_EQ(state.cards_played, 1u); EXPECT_EQ(state.energy, 2);
        EXPECT_TRUE(valid_state(state));
    }
}

TEST(CardChoices, PurityRetainsAcrossTurnsAndRecordsEachExhaustBeforeSourceFinishes) {
    const std::array deck{CardSpec{CardId::purity}, CardSpec{CardId::defend}};
    CombatConfig config; config.deck = deck;
    State state; Scratch scratch; Simulator simulator;
    ASSERT_EQ(simulator.initialize(state, config, scratch).status, StepStatus::awaiting_input);
    const auto source = *std::find_if(state.zone(Zone::hand).begin(), state.zone(Zone::hand).end(), [&](auto h) { return state.cards.get(h)->definition == CardId::purity; });
    ASSERT_EQ(simulator.apply(state, {state.boundary, InputKind::end_turn}, scratch).status, StepStatus::awaiting_input);
    EXPECT_NE(std::find(state.zone(Zone::hand).begin(), state.zone(Zone::hand).end(), source), state.zone(Zone::hand).end());
    ASSERT_EQ(simulator.apply(state, {state.boundary, InputKind::play_card, source}, scratch).status, StepStatus::awaiting_input);
    ASSERT_EQ(state.phase, Phase::card_choice);
    const auto chosen = state.selection.candidates.front();
    ASSERT_EQ(simulator.apply(state, {state.boundary, InputKind::select_card, chosen, false, state.selection.id}, scratch).status, StepStatus::awaiting_input);
    ASSERT_EQ(simulator.apply(state, {state.boundary, InputKind::confirm_selection, {}, false, state.selection.id}, scratch).status, StepStatus::awaiting_input);
    ASSERT_GE(state.history.size(), 3u);
    const auto n = state.history.size();
    EXPECT_EQ(state.history[n - 3].kind, HistoryKind::exhausted);
    EXPECT_EQ(state.history[n - 3].card, chosen);
    EXPECT_EQ(state.history[n - 2].kind, HistoryKind::play_finished);
    EXPECT_EQ(state.history[n - 1].kind, HistoryKind::exhausted);
    EXPECT_EQ(state.history[n - 1].card, source);
}

TEST(CardChoices, EmptyAndForcedSelectionsResolveWithoutPublishingPrompts) {
    for (auto id : {CardId::purity, CardId::armaments, CardId::headbutt, CardId::burning_pact, CardId::true_grit}) {
        for (const bool upgraded : {false, true}) {
            const std::array deck{CardSpec{id, upgraded}};
            CombatConfig config; config.deck = deck;
            State state; Scratch scratch; Simulator simulator;
            ASSERT_EQ(simulator.initialize(state, config, scratch).status, StepStatus::awaiting_input);
            const auto source = state.zone(Zone::hand).front();
            ASSERT_EQ(simulator.apply(state, {state.boundary, InputKind::play_card, source, id == CardId::headbutt}, scratch).status, StepStatus::awaiting_input);
            EXPECT_EQ(state.phase, Phase::play);
            EXPECT_EQ(state.next_prompt.value, 1u);
            EXPECT_TRUE(valid_state(state));
        }
    }
    for (auto id : {CardId::armaments, CardId::burning_pact, CardId::true_grit}) {
        const std::array deck{CardSpec{id, true}, CardSpec{CardId::strike}};
        CombatConfig config; config.deck = deck;
        State state; Scratch scratch; Simulator simulator;
        ASSERT_EQ(simulator.initialize(state, config, scratch).status, StepStatus::awaiting_input);
        const auto source = *std::find_if(state.zone(Zone::hand).begin(), state.zone(Zone::hand).end(), [&](auto h) { return state.cards.get(h)->definition == id; });
        ASSERT_EQ(simulator.apply(state, {state.boundary, InputKind::play_card, source}, scratch).status, StepStatus::awaiting_input);
        EXPECT_EQ(state.phase, Phase::play);
        EXPECT_TRUE(valid_state(state));
    }
}

TEST(CardChoices, CheckpointRejectsOrphanedSelectionsAndInvalidPromptCounters) {
    State state; Scratch scratch;
    ASSERT_EQ(Simulator{}.initialize(state, {}, scratch).status, StepStatus::awaiting_input);
    state.selection.id = PromptId{1};
    EXPECT_FALSE(valid_state(state));
    EXPECT_THROW((void)checkpoint(state), std::invalid_argument);
    state.selection = {}; state.next_prompt.value = 0;
    EXPECT_FALSE(valid_state(state));
}

}
