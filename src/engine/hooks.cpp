#include "gameplay.hpp"
#include "sts2/core/sort.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace sts2::engine::gameplay {
PowerModifier before_power_application(const CreatureState& creature, PowerId id, int amount) noexcept {
    const bool debuff = (id == PowerId::strength || id == PowerId::dexterity) ? amount < 0 :
        id == PowerId::shrink || ((id == PowerId::weak || id == PowerId::vulnerable) && amount >= 0);
    const auto artifact = creature.has_power(PowerId::artifact) && debuff ? creature.power(PowerId::artifact).instance : UINT32_MAX;
    return {artifact == UINT32_MAX ? amount : 0, artifact};
}
void after_power_application(CombatView state, Scratch& scratch, CreatureState& creature, PowerModifier modifier, bool enemy, bool enemy_side) {
    if (modifier.artifact != UINT32_MAX && creature.has_power(PowerId::artifact) && creature.power(PowerId::artifact).instance == modifier.artifact)
        (void)apply_power(state, scratch, PowerId::artifact, -1, enemy, enemy_side, {}, false);
}
std::int64_t modify_block(const CreatureState& creature, int amount) noexcept {
    return std::max<std::int64_t>(0, std::int64_t(amount) + creature.power_amount(PowerId::dexterity));
}
int modify_attack_damage(const CreatureState& target, int base, const CreatureState& source) noexcept {
    // Native decimal modifiers combine before one floor operation.
    const std::int64_t numerator = std::max<std::int64_t>(0, std::int64_t(base) + source.power_amount(PowerId::strength)) *
        (source.has_power(PowerId::shrink) ? 70 : 100) * (source.power_amount(PowerId::weak) > 0 ? 75 : 100) *
        (target.power_amount(PowerId::vulnerable) > 0 ? 150 : 100);
    return static_cast<int>(numerator / 1000000);
}
void after_card_exhausted(CombatView state, Scratch& scratch, CardHandle card) {
    if (!state.shuffle_order.empty() && state.exhaust_relic_count != 0) {
        EffectFrame frame;
        frame.source = card;
        if (!state.execution.empty()) { frame.play = state.execution.back().play; frame.energy = state.execution.back().energy; }
        frame.pending = ExhaustContinuation{state.exhaust_relics, state.exhaust_relic_count, 0};
        state.execution.push_back(frame);
        return;
    }
    // Guaranteed synchronous callers need no continuation frame.
    const auto candidates = state.exhaust_relics;
    const auto count = state.exhaust_relic_count;
    for (std::uint8_t i = 0; i < count; ++i) react_to_exhaust(candidates[i], state, scratch);
}
void after_enemy_turn(CombatView state, Scratch& scratch) {
    // Capture native application order before callbacks can change membership.
    for (auto* owner : {&state.player, &state.enemy}) {
        const auto candidates = owner->power_order;
        const auto count = owner->power_count;
        for (std::size_t i = 0; i < count; ++i)
            if (candidates[i] == PowerId::weak || candidates[i] == PowerId::vulnerable)
                expire_power(state, scratch, *owner, candidates[i]);
    }
}
}
