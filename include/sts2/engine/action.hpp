#pragma once
#include "sts2/engine/run.hpp"
#include <optional>
#include <vector>

namespace sts2::engine {
struct ActionView { CombatInput input; Restriction restriction; };
class ActionSpaceView {
public:
    explicit ActionSpaceView(ConstCombatView state) noexcept : state_(state) {}
    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] std::optional<ActionView> at(std::size_t index) const noexcept;
private:
    ConstCombatView state_;
};

// Inputs retain the current boundary. Forced transport continues are omitted.
struct PlayerAction { PlayerInput input; Restriction restriction; };
// Borrowed indexed traversal. Reacquire after mutation; no state copy or allocation.
class RunActionSpaceView {
public:
    explicit RunActionSpaceView(const RunState&) noexcept;
    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] bool empty() const noexcept { return size() == 0; }
    [[nodiscard]] std::optional<PlayerAction> at(std::size_t index) const noexcept;
private:
    const RunState& state_;
    BoundaryId boundary_;
    bool valid_;
};
// Lists offered actions, including restricted actions and their reasons.
[[nodiscard]] std::vector<PlayerAction> get_actions(const RunState&);
// Travel validation checks visible legality; hidden capability is checked on apply.
[[nodiscard]] Restriction validate_action(const RunState&, const PlayerInput&) noexcept;
// Apply one player action and advance automatically to a decision or terminal result.
[[nodiscard]] StepResult apply_action(RunState&, const PlayerInput&, Scratch&);
// Execute automatic transitions until a decision or terminal result is reached.
[[nodiscard]] StepResult advance_to_decision(RunState&, Scratch&);
} // namespace sts2::engine
