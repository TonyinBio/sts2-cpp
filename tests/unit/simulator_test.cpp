#include "sts2/simulator.hpp"
#include <gtest/gtest.h>

namespace sts2 {
namespace {

TEST(SimulatorTest, InitialActionsIncludeEveryCardAndEndTurn) {
    const auto actions = legal_actions(CombatState{});
    ASSERT_EQ(actions.size, 6u);
    for (std::size_t i = 0; i < 5; ++i) {
        EXPECT_EQ(actions.actions[i].kind, ActionKind::play_card);
        EXPECT_EQ(actions.actions[i].card_index, i);
    }
    EXPECT_EQ(actions.actions[5].kind, ActionKind::end_turn);
}

TEST(SimulatorTest, InvalidCardIndexLeavesStateUnchanged) {
    CombatState state;
    const auto before = state;
    EXPECT_FALSE(step(state, {ActionKind::play_card, 5}));
    EXPECT_EQ(state.player_hp, before.player_hp);
    EXPECT_EQ(state.player_block, before.player_block);
    EXPECT_EQ(state.enemy_hp, before.enemy_hp);
    EXPECT_EQ(state.energy, before.energy);
    EXPECT_EQ(state.turn, before.turn);
    EXPECT_EQ(state.hand, before.hand);
    EXPECT_EQ(state.hand_size, before.hand_size);
    EXPECT_EQ(state.outcome, before.outcome);
}

TEST(SimulatorTest, AttackCostsEnergyAndRemovesCard) {
    CombatState state;
    ASSERT_TRUE(step(state, {ActionKind::play_card, 0}));
    EXPECT_EQ(state.enemy_hp, 18);
    EXPECT_EQ(state.energy, 2);
    EXPECT_EQ(state.hand_size, 4);
    EXPECT_EQ(state.hand[0], Card::defend);
}

TEST(SimulatorTest, DefendBlocksDamageUntilNextTurn) {
    CombatState state;
    ASSERT_TRUE(step(state, {ActionKind::play_card, 1}));
    EXPECT_EQ(state.player_block, 5);
    ASSERT_TRUE(step(state, {ActionKind::end_turn}));
    EXPECT_EQ(state.player_hp, 27);
    EXPECT_EQ(state.player_block, 0);
    EXPECT_EQ(state.energy, 3);
    EXPECT_EQ(state.hand_size, 5);
    EXPECT_EQ(state.turn, 2);
    EXPECT_EQ(state.hand, CombatState{}.hand);
}

TEST(SimulatorTest, LethalAttackEndsCombat) {
    CombatState state;
    state.enemy_hp = 6;
    ASSERT_TRUE(step(state, {ActionKind::play_card, 0}));
    EXPECT_EQ(state.enemy_hp, 0);
    EXPECT_EQ(state.outcome, Outcome::victory);
    EXPECT_EQ(legal_actions(state).size, 0u);
    EXPECT_FALSE(step(state, {ActionKind::end_turn}));
}

TEST(SimulatorTest, LethalEnemyAttackEndsCombat) {
    CombatState state;
    state.player_hp = 8;
    ASSERT_TRUE(step(state, {ActionKind::end_turn}));
    EXPECT_EQ(state.player_hp, 0);
    EXPECT_EQ(state.outcome, Outcome::defeat);
    EXPECT_EQ(legal_actions(state).size, 0u);
    EXPECT_FALSE(step(state, {ActionKind::play_card, 0}));
}

} // namespace
} // namespace sts2
