#include "sts2/simulator.hpp"
#include <algorithm>
#include <limits>

namespace sts2 {
namespace {
void shuffle(CombatState& state, Pile& pile) noexcept {
    // .NET sorts ten-card lists with insertion sort. Preserve order among equal
    // models; do not introduce a card-instance tiebreaker.
    for (std::uint8_t i = 1; i < pile.size; ++i) {
        const auto card = pile.cards[i];
        auto j = i;
        while (j > 0 && state.cards[pile.cards[j - 1]] > state.cards[card]) {
            pile.cards[j] = pile.cards[j - 1];
            --j;
        }
        pile.cards[j] = card;
    }
    auto& rng = state.shuffle_rng;
    for (int i = pile.size - 1; i > 0; --i) {
        const auto j = rng.next_int(i + 1);
        std::swap(pile.cards[i], pile.cards[j]);
    }
}
void draw_hand(CombatState& state) noexcept {
    for (int i = 0; i < 5; ++i) {
        if (state.draw.size == 0) {
            state.draw = state.discard;
            state.discard.clear();
            shuffle(state, state.draw);
        }
        if (state.draw.size == 0) break;
        state.hand.push(state.draw.erase(0));
    }
}
void apply_vulnerable(CombatState& state) noexcept {
    if (state.vulnerable.amount == 0) {
        state.vulnerable = {};
        state.vulnerable.instance = state.next_power_instance++;
    }
    state.vulnerable.amount += 2;
}
void victory(CombatState& state) noexcept {
    state.outcome = Outcome::victory;
    state.player_hp = std::min(state.player_max_hp, state.player_hp + 6); // Burning Blood
    state.player_block = 0;
    state.shrink = {};
    state.vulnerable = {};
    state.hand.clear();
    state.draw.clear();
    state.discard.clear();
    state.card_count = 0; // matches post-victory cleanup in the reference
}
} // namespace

CombatState make_starter_combat(std::uint64_t seed) noexcept {
    CombatState state;
    state.shuffle_rng = Rng::seeded(seed);
    state.cards = {Card::strike, Card::strike, Card::strike, Card::strike, Card::strike,
                   Card::defend, Card::defend, Card::defend, Card::defend, Card::bash};
    for (CardId i = 0; i < max_cards; ++i) state.draw.push(i);
    shuffle(state, state.draw);
    draw_hand(state);
    return state;
}

LegalActions legal_actions(const CombatState& state) noexcept {
    LegalActions result;
    if (state.outcome != Outcome::ongoing) return result;
    for (std::uint8_t i = 0; i < state.hand.size; ++i) {
        const auto card = state.cards[state.hand.cards[i]];
        if (card_cost(card) <= state.energy)
            result.actions[result.size++] = {ActionKind::play_card, i,
                static_cast<std::int8_t>(card == Card::defend ? -1 : 0)};
    }
    result.actions[result.size++] = {ActionKind::end_turn};
    return result;
}

bool step(CombatState& state, Action action) noexcept {
    if (state.outcome != Outcome::ongoing) return false;
    if (action.kind == ActionKind::play_card) {
        if (action.card_index >= state.hand.size) return false;
        const auto id = state.hand.cards[action.card_index];
        const auto card = state.cards[id];
        if (card_cost(card) > state.energy || action.target != (card == Card::defend ? -1 : 0)) return false;
        if (card == Card::bash && state.vulnerable.amount > std::numeric_limits<std::int32_t>::max() - 2) return false;
        state.energy -= card_cost(card);
        (void)state.hand.erase(action.card_index);
        if (card == Card::defend) state.player_block += 5;
        else {
            // Apply multiplicative effects together, then floor once, as the game does.
            const int base = card == Card::bash ? 8 : 6;
            const int damage = base * (state.shrink.amount ? 7 : 10) * (state.vulnerable.amount ? 15 : 10) / 100;
            const int blocked = std::min(state.enemy_block, damage);
            state.enemy_block -= blocked;
            state.enemy_hp = std::max(0, state.enemy_hp - (damage - blocked));
            if (state.enemy_hp == 0) { victory(state); return true; }
            if (card == Card::bash) apply_vulnerable(state);
        }
        state.discard.push(id);
        return true;
    }
    if (action.kind != ActionKind::end_turn || action.target != -1 || action.card_index != 0 ||
        state.turn == std::numeric_limits<std::uint32_t>::max()) return false;
    for (std::uint8_t i = 0; i < state.hand.size; ++i) state.discard.push(state.hand.cards[i]);
    state.hand.clear();
    state.enemy_block = 0;
    if (state.vulnerable.amount) state.vulnerable.amount_on_turn_start = state.vulnerable.amount;
    ++state.moves_performed;
    state.current_move_performed = true;
    if (state.enemy_move == Move::shrink) {
        state.shrink = {-1, 0, state.next_power_instance++, true};
    } else {
        const int damage = state.enemy_move == Move::chomp ? 7 : 13;
        const int blocked = std::min(state.player_block, damage);
        state.player_block -= blocked;
        state.player_hp = std::max(0, state.player_hp - (damage - blocked));
        if (state.player_hp == 0) {
            state.outcome = Outcome::defeat;
            state.shrink = {}; // powers are removed on player death
            return true;
        }
    }
    if (state.vulnerable.amount) {
        if (state.vulnerable.skip_next_duration_tick) state.vulnerable.skip_next_duration_tick = false;
        else if (--state.vulnerable.amount == 0) state.vulnerable = {};
    }
    ++state.turn;
    state.enemy_move = state.enemy_move == Move::chomp ? Move::stomp : Move::chomp;
    state.current_move_performed = false;
    state.player_block = 0;
    state.energy = state.max_energy;
    if (state.shrink.amount) state.shrink.amount_on_turn_start = state.shrink.amount;
    draw_hand(state);
    return true;
}
} // namespace sts2
