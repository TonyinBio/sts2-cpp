#include "sts2/simulator.hpp"
#include <gtest/gtest.h>

namespace sts2 {
namespace {
CombatState combat() {
    auto s = make_starter_combat();
    // A specific valid hand for isolated mechanic tests; remaining cards in draw.
    s.hand.clear(); s.draw.clear(); s.discard.clear();
    s.cards = {Card::strike, Card::defend, Card::bash, Card::strike, Card::defend,
               Card::strike, Card::strike, Card::strike, Card::defend, Card::defend};
    for (CardId i = 0; i < 5; ++i) s.hand.push(i);
    for (CardId i = 5; i < 10; ++i) s.draw.push(i);
    return s;
}
TEST(SimulatorTest, EnumeratesAffordableCardsWithExplicitTargets) {
    auto s = combat(); s.energy = 1;
    const auto actions = legal_actions(s);
    ASSERT_EQ(actions.size, 5u);
    EXPECT_EQ(actions.actions[0], (Action{ActionKind::play_card, 0, 0}));
    EXPECT_EQ(actions.actions[1], (Action{ActionKind::play_card, 1, -1}));
    EXPECT_EQ(actions.actions[4].kind, ActionKind::end_turn);
}
TEST(SimulatorTest, InvalidActionDoesNotMutateStateOrRng) {
    auto s = combat(); const auto original = s;
    EXPECT_FALSE(step(s, {ActionKind::play_card, 5, 0}));
    EXPECT_FALSE(step(s, {ActionKind::play_card, 0, -1}));
    EXPECT_FALSE(step(s, {ActionKind::play_card, 1, 0}));
    EXPECT_FALSE(step(s, {static_cast<ActionKind>(99)}));
    EXPECT_FALSE(step(s, {ActionKind::end_turn, 1}));
    EXPECT_EQ(s, original);
    s.energy = 1; const auto before = s;
    EXPECT_FALSE(step(s, {ActionKind::play_card, 2, 0}));
    EXPECT_EQ(s, before);
}
TEST(SimulatorTest, AttackSpendsEnergyAndMovesTheSameInstanceToDiscard) {
    auto s = combat();
    ASSERT_TRUE(step(s, {ActionKind::play_card, 0, 0}));
    EXPECT_EQ(s.enemy_hp, 32); EXPECT_EQ(s.energy, 2);
    EXPECT_EQ(s.hand.cards[0], 1); EXPECT_EQ(s.discard.cards[0], 0);
}
TEST(SimulatorTest, BashDamagePrecedesVulnerableAndRoundsOnlyOnce) {
    auto s = combat();
    ASSERT_TRUE(step(s, {ActionKind::play_card, 2, 0}));
    EXPECT_EQ(s.enemy_hp, 30); EXPECT_EQ(s.vulnerable.amount, 2);
    s.energy = 3; s.shrink.amount = -1;
    ASSERT_TRUE(step(s, {ActionKind::play_card, 0, 0}));
    EXPECT_EQ(s.enemy_hp, 24); // floor(6 * .7 * 1.5) = 6, not floor(4.2)*1.5 by accident
}
TEST(SimulatorTest, BlockAbsorbsAnEnemyAttackAndClearsAtPlayerTurnStart) {
    auto s = combat(); s.enemy_move = Move::chomp; s.moves_performed = 1; s.turn = 2;
    ASSERT_TRUE(step(s, {ActionKind::play_card, 1}));
    ASSERT_EQ(s.player_block, 5);
    ASSERT_TRUE(step(s, {ActionKind::end_turn}));
    EXPECT_EQ(s.player_hp, 78); EXPECT_EQ(s.player_block, 0);
    EXPECT_EQ(s.turn, 3u); EXPECT_EQ(s.enemy_move, Move::stomp);
}
TEST(SimulatorTest, VictoryHealsClearsPilesAndDisablesFurtherActions) {
    auto s = combat(); s.enemy_hp = 6; s.player_hp = 70;
    ASSERT_TRUE(step(s, {ActionKind::play_card, 0, 0}));
    EXPECT_EQ(s.outcome, Outcome::victory); EXPECT_EQ(s.player_hp, 76);
    EXPECT_EQ(s.hand.size + s.draw.size + s.discard.size, 0);
    EXPECT_EQ(s.card_count, 0); EXPECT_EQ(legal_actions(s).size, 0u);
    const auto before = s;
    EXPECT_FALSE(step(s, {ActionKind::end_turn})); EXPECT_EQ(s, before);
}
TEST(SimulatorTest, LethalEnemyTurnRetainsPilesButStopsBeforeDrawing) {
    auto s = combat(); s.player_hp = 7; s.enemy_move = Move::chomp;
    s.moves_performed = 1; s.turn = 2; s.shrink.amount = -1;
    ASSERT_TRUE(step(s, {ActionKind::end_turn}));
    EXPECT_EQ(s.outcome, Outcome::defeat); EXPECT_EQ(s.player_hp, 0);
    EXPECT_EQ(s.turn, 2u); EXPECT_EQ(s.hand.size, 0); EXPECT_EQ(s.discard.size, 5);
    EXPECT_EQ(s.draw.size, 5); EXPECT_EQ(s.shrink.amount, 0);
    EXPECT_TRUE(s.current_move_performed);
}
TEST(SimulatorTest, CopiedBranchesAreIndependentAndReproducible) {
    const auto initial = combat();
    auto left = initial, right = initial;
    ASSERT_TRUE(step(left, {ActionKind::end_turn}));
    ASSERT_TRUE(step(left, {ActionKind::end_turn}));
    EXPECT_EQ(right, initial);
    ASSERT_TRUE(step(right, {ActionKind::end_turn}));
    ASSERT_TRUE(step(right, {ActionKind::end_turn}));
    EXPECT_EQ(left, right);
}
TEST(SimulatorTest, LegalRolloutsConserveCardsAndTerminateAcrossSeeds) {
    for (std::uint64_t seed = 0; seed < 256; ++seed) {
        auto s = make_starter_combat(seed);
        for (int i = 0; i < 100 && s.outcome == Outcome::ongoing; ++i) {
            const auto actions = legal_actions(s);
            ASSERT_TRUE(step(s, actions.actions[0]));
            if (s.outcome == Outcome::victory) continue;
            ASSERT_EQ(s.hand.size + s.draw.size + s.discard.size, 10);
            std::array<bool, max_cards> seen{};
            for (const auto* pile : {&s.hand, &s.draw, &s.discard})
                for (std::uint8_t j = 0; j < pile->size; ++j) {
                    ASSERT_LT(pile->cards[j], max_cards);
                    EXPECT_FALSE(seen[pile->cards[j]]);
                    seen[pile->cards[j]] = true;
                }
        }
        EXPECT_NE(s.outcome, Outcome::ongoing);
    }
}
} // namespace
} // namespace sts2
