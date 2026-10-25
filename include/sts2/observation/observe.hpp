#pragma once
#include "sts2/kernel/simulator.hpp"
#include <optional>
#include <string>

namespace sts2::engine {
struct ObservationView {
    std::string_view build;
    BoundaryId boundary;
    Phase phase;
    std::int32_t hp, max_hp, block, energy, turn, enemy_hp, enemy_max_hp;
    std::int32_t enemy_vulnerable, enemy_attack;
    bool shrunk;
    BeetleMove intent;
    std::span<const CardHandle> hand;
    std::size_t draw_count, discard_count, exhaust_count;
    std::int32_t strength, dexterity, weak, enemy_weak;
};
// Borrows hand storage; invalidated by state mutation. Hidden draw order, RNG,
// execution frames and unrevealed data are never part of the public projection.
[[nodiscard]] ObservationView observe(const State&) noexcept;
struct CardView {
    CardHandle handle;
    std::string_view model_id, name;
    std::int32_t cost, damage, block, vulnerable;
    bool upgraded, enemy_target;
    std::int32_t strength = 0, weak = 0;
    bool exhaust = false, power = false;
};
[[nodiscard]] std::optional<CardView> observe_card(const State&, CardHandle, const Ruleset& = Ruleset::pinned()) noexcept;
// Metadata for inspectable hand/draw/discard/exhaust cards; never pile position.
[[nodiscard]] std::optional<CardView> inspect_card(const State&, CardHandle, const Ruleset& = Ruleset::pinned()) noexcept;
// English rendering is outside the kernel; callers pay for it only on demand.
[[nodiscard]] std::string describe(const CardView&);
struct ActionView { Input input; Restriction restriction; };
class ActionSpaceView {
public:
    explicit ActionSpaceView(const State& state) noexcept : state_(state) {}
    [[nodiscard]] std::size_t size() const noexcept;
    [[nodiscard]] std::optional<ActionView> at(std::size_t index) const noexcept;
private:
    const State& state_;
};
} // namespace sts2::engine
