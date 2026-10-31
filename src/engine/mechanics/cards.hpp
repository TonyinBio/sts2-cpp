#pragma once
#include "sts2/engine/run.hpp"

namespace sts2::engine::gameplay {
// Borrowed pointers are invalidated by growth of their side store.
CardExtraData* find_card_extra(CombatView, CardHandle) noexcept;
const CardExtraData* find_card_extra(ConstCombatView, CardHandle) noexcept;
CardExtraData* find_card_extra(RunView, PersistentCardHandle) noexcept;
const CardExtraData* find_card_extra(ConstRunView, PersistentCardHandle) noexcept;
// One payload per live generational handle. Insertion can overflow inline storage.
void set_card_extra(CombatView, CardHandle, CardExtraData);
void set_card_extra(RunView, PersistentCardHandle, CardExtraData);
bool valid_card_extra(ConstCombatView) noexcept;
bool valid_card_extra(ConstRunView) noexcept;
void move_card(CombatView, Zone from, Zone to, CardHandle);
void discard_card(CombatView, Scratch&, CardHandle);
void discard_hand_at_turn_end(CombatView, const Ruleset&);
void exhaust(CombatView, Scratch&, Zone, CardHandle);
void reshuffle(CombatView, const Ruleset&);
void draw_one(CombatView, Scratch&, bool from_hand_draw);
void draw(CombatView, Scratch&, int count, const Ruleset&, bool from_hand_draw = true);
}

namespace sts2::engine {
struct RunState;
namespace detail {
// Upgrade a combat card and its persistent origin.
[[nodiscard]] Restriction upgrade_card_permanently(RunState&, CardHandle) noexcept;
}
}
