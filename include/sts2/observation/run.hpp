#pragma once
#include "sts2/engine/run.hpp"
#include "sts2/observation/observe.hpp"
#include <variant>
#include <vector>

namespace sts2::engine {
// Inputs retain the current boundary. Forced transport continues are omitted.
struct PlayerAction { PlayerInput input; Restriction restriction; };
struct VisibleCard {
    std::uint32_t instance;
    std::string_view model_id;
    std::uint32_t upgrade;
    std::optional<std::int32_t> energy_cost;
    friend bool operator==(const VisibleCard&, const VisibleCard&) = default;
};
struct VisibleOption {
    std::uint32_t id;
    std::string_view model_id;
};
struct VisibleMapNode {
    MapNodeId id;
    std::int32_t col, row;
    MapKind kind;
    std::vector<MapNodeId> children;
    bool visited, current, available;
    friend bool operator==(const VisibleMapNode&, const VisibleMapNode&) = default;
};
enum class RewardKind : std::uint8_t { gold, potion, cards };
struct VisibleReward {
    std::uint32_t id;
    RewardKind kind;
    std::int32_t gold = 0;
    std::string_view model_id;
    bool claimed = false;
};
enum class PublicPrompt : std::uint8_t { none, neow, map, combat, discard, rewards, card_reward };
struct RunObservation {
    std::uint32_t contract_revision = 2;
    std::string_view build = game_build;
    BoundaryId boundary;
    RunPhase phase;
    bool valid;
    PublicPrompt prompt = PublicPrompt::none;
    std::uint32_t minimum_selection = 0, maximum_selection = 0;
    bool submits_immediately = true;
    std::string_view character, seed;
    std::uint8_t ascension, act;
    std::int32_t hp, max_hp, gold, potion_capacity;
    std::vector<VisibleCard> deck;
    std::vector<VisibleOption> relics, potions, options;
    std::vector<VisibleMapNode> map;
    std::vector<MapNodeId> path;
    std::vector<VisibleReward> rewards;
    std::vector<VisibleCard> reward_cards;
    bool selecting_reward_card = false;
    bool can_skip_rewards = false;
    bool can_skip_card_reward = false;
    std::optional<CombatObservationView> combat;
    std::vector<CardView> hand_cards;
    // Inspectable piles only; draw order and hidden assignments are excluded.
    std::vector<CardView> draw, discard, exhaust;
};
// Owns projected containers, borrows seed and combat hand until state mutation.
[[nodiscard]] RunObservation observe(const RunState&);
[[nodiscard]] std::vector<PlayerAction> actions(const RunState&);
// Travel validation checks visible legality; hidden capability is checked on apply.
[[nodiscard]] Restriction validate_player(const RunState&, const PlayerInput&);
[[nodiscard]] StepResult apply_player(RunState&, const PlayerInput&, Scratch&);
[[nodiscard]] StepResult advance_player(RunState&, Scratch&);
[[nodiscard]] std::string describe(const RunObservation&);
[[nodiscard]] std::string describe(Restriction);
struct DefinitionText {
    std::string name, description;
    bool supported = false;
};
// Description support is independent of acquisition/play support.
[[nodiscard]] DefinitionText describe(const VisibleOption&);
[[nodiscard]] DefinitionText describe(const VisibleCard&);
} // namespace sts2::engine
