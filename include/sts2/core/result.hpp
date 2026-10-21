#pragma once
#include "sts2/core/ids.hpp"
#include <cstdint>

namespace sts2::engine {
enum class StepStatus : std::uint8_t {
    awaiting_input, combat_won, player_died, invalid_action, unsupported_content,
    resource_limit, engine_error
};
enum class Restriction : std::uint8_t {
    none, stale_boundary, wrong_phase, malformed_action, stale_card,
    invalid_target, insufficient_energy, invalid_selection, invalid_state,
    unsupported_setup, execution_budget, capacity, clock_exhausted
};
struct StepResult {
    StepStatus status;
    BoundaryId boundary;
    Restriction reason = Restriction::none;
};
} // namespace sts2::engine
