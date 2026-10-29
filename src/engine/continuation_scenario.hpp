#pragma once
#include "sts2/engine/run.hpp"
#include <stdexcept>

// Controlled architecture fixtures, deliberately outside the consumer API.
namespace sts2::engine::scenario {
using ShuffleReactionKind = sts2::engine::ShuffleReactionKind;
inline void configure_shuffle_reactions(RunState& state, std::span<const ShuffleReactionKind> reactions, bool remember_choice = false) {
    auto combat = *state.view().combat;
    for (auto kind : reactions) {
        const auto handle = combat.shuffle_reactions.create({kind, false, remember_choice}, UINT32_MAX);
        if (!handle.present()) throw std::length_error("Shuffle reaction identity exhausted");
        combat.shuffle_order.push_back(handle);
    }
}
}
