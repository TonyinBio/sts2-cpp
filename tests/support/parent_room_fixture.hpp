#pragma once
#include "sts2/engine/run.hpp"
#include <stdexcept>

namespace sts2::engine::scenario {
// Controlled enclosing interaction; no native event is added to content.
inline void configure_parent_return(RunState& state, std::int32_t gold) {
    auto view = state.view();
    if (view.phase != RunPhase::combat || !view.combat || view.direct_combat || !std::holds_alternative<std::monostate>(view.room) || gold < 0)
        throw std::invalid_argument("Parent interaction requires an independent active child combat");
    view.room = ParentInteractionState{ParentRoomStage::child_combat, gold};
}
}
