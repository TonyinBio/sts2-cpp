#include "relics.hpp"
#include "../mechanics/damage.hpp"
#include "../mechanics/cards.hpp"
#include "../combat/trace.hpp"

namespace sts2::engine::gameplay {
void react_to_exhaust(ExhaustRelic listener, CombatView state, Scratch& scratch) {
    if (listener == ExhaustRelic::charons_ashes && state.enemy.hp > 0) {
        const auto hit = receive_damage(state.enemy, 3);
        emit(state, scratch, CombatEventKind::damaged, false, true, {}, 4, 0, hit);
    } else if (listener == ExhaustRelic::joss_paper) {
        ++state.joss_paper_counter;
        if (state.joss_paper_counter >= 5) {
            if (!state.shuffle_order.empty()) {
                EffectFrame completion;
                if (!state.execution.empty()) completion = state.execution.back();
                completion.pending = RelicDrawCompletion{};
                state.execution.push_back(completion);
                draw(state, scratch, static_cast<int>(state.joss_paper_counter / 5), Ruleset::pinned(), false);
                return;
            }
            draw(state, scratch, static_cast<int>(state.joss_paper_counter / 5), Ruleset::pinned(), false);
            state.joss_paper_counter %= 5;
        }
    }
}
}
