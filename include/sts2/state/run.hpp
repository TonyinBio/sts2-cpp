#pragma once
#include "sts2/state/setup.hpp"
#include "sts2/state/state.hpp"
#include <optional>

namespace sts2::engine {
inline constexpr std::uint32_t max_retained_encounters = 4096;
enum class RunPhase : std::uint8_t { uninitialized, neow, neow_done, map, combat, combat_done, rewards, complete, invalid };
struct CombatRewards {
    std::int32_t gold = 0;
    ContentId potion = no_content;
    std::array<PersistentCard, 3> cards{};
    bool gold_taken = false, potion_taken = false, card_taken = false;
    bool selecting_card = false, resolved = false, skipped = false;
    std::uint8_t selected_card = UINT8_MAX;
    friend bool operator==(const CombatRewards&, const CombatRewards&) = default;
};
// A completed encounter owns its combat graph and captured inventory descriptors.
// Handles inside history remain aliases into that graph, never the next combat.
struct EncounterRecord {
    State combat;
    std::optional<CombatRewards> rewards;
    std::vector<PersistentCard> deck;
    std::vector<ContentId> discarded_potions;
    MapNodeId node = no_node;
    std::uint32_t floor = 0;
    friend bool operator==(const EncounterRecord&, const EncounterRecord&) = default;
};
struct RunState {
    SetupState setup;
    std::optional<State> combat;
    bool direct_combat = false;
    std::vector<EncounterRecord> encounters;
    std::optional<CombatRewards> rewards;
    std::vector<ContentId> discarded_potions;
    RunClock clock;
    BoundaryId boundary{};
    RunPhase phase = RunPhase::uninitialized;
    std::uint8_t act_index = 0;
    std::array<std::optional<MapState>, 3> maps;
    MapNodeId current = no_node;
    std::vector<MapNodeId> visited;
    GameRng event_rng;
    std::array<ContentId, 3> neow_options{};
    ContentId selected_neow = no_content;
    std::int32_t initial_heal = 0, gold_gained = 0;
    friend bool operator==(const RunState&, const RunState&) = default;
};
} // namespace sts2::engine
