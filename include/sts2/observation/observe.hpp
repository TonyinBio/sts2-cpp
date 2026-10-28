#pragma once
#include "sts2/engine/combat.hpp"
#include <optional>
#include <string>

namespace sts2::engine {
struct CardSelectionView {
    PromptId id;
    SelectionEffect effect;
    std::span<const CardHandle> candidates, selected;
    std::uint32_t minimum, maximum;
    bool submits_immediately, can_confirm;
};
struct CombatObservationView {
    std::string_view build;
    BoundaryId boundary;
    CombatPhase phase;
    std::int32_t hp, max_hp, block, energy, turn, enemy_hp, enemy_max_hp;
    std::int32_t enemy_vulnerable, enemy_attack;
    bool shrunk;
    BeetleMove intent;
    std::span<const CardHandle> hand;
    std::size_t draw_count, discard_count, exhaust_count;
    std::optional<CardSelectionView> selection;
    std::int32_t player_strength, player_dexterity, player_weak, enemy_weak;
};
// Borrows hand storage; invalidated by state mutation. Hidden draw order, RNG,
// execution frames and unrevealed data are never part of the public projection.
[[nodiscard]] CombatObservationView observe(ConstCombatView) noexcept;
struct CardView {
    CardHandle handle;
    std::string_view model_id, name;
    std::int32_t cost, damage, block, vulnerable;
    bool upgraded, enemy_target;
    CardRarity rarity = CardRarity::basic;
    std::int32_t strength = 0, weak = 0;
    bool exhaust = false, power = false;
};
[[nodiscard]] std::optional<CardView> observe_card(ConstCombatView, CardHandle, const Ruleset& = Ruleset::pinned()) noexcept;
// Metadata for inspectable hand/draw/discard/exhaust cards; never pile position.
[[nodiscard]] std::optional<CardView> inspect_card(ConstCombatView, CardHandle, const Ruleset& = Ruleset::pinned()) noexcept;
// English rendering is outside the kernel; callers pay for it only on demand.
[[nodiscard]] std::string describe(const CardView&);
} // namespace sts2::engine
