#include "damage.hpp"
#include "../content/powers.hpp"
#include <algorithm>

namespace sts2::engine::gameplay {
HitResult damage(CreatureState& target, int base, const CreatureState& source, const OverflowStorage& owner) noexcept {
    return receive_damage(target, modify_attack_damage(target, base, source, owner));
}
HitResult receive_damage(CreatureState& target, int amount) noexcept {
    const auto blocked = std::min(target.block, amount);
    const auto unblocked = std::min(target.hp, amount - blocked);
    const HitResult hit{blocked, unblocked, amount - blocked - unblocked,
        target.block > 0 && blocked == target.block, blocked >= amount, unblocked == target.hp};
    target.block -= blocked; target.hp -= unblocked;
    if (unblocked > 0) ++target.positive_unblocked_hits;
    return hit;
}
int heal(CreatureState& creature, int amount) noexcept {
    const auto healed = std::min(std::max(0, amount), creature.max_hp - creature.hp);
    creature.hp += healed; return healed;
}
}
