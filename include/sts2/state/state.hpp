#pragma once
#include "sts2/core/result.hpp"
#include "sts2/core/storage.hpp"
#include "sts2/rules/ruleset.hpp"
#include "sts2/state/clock.hpp"
#include "sts2/state/rng.hpp"
#include "sts2/state/history.hpp"
#include <array>
#include <vector>
#include <string>

namespace sts2::engine {
enum class Zone : std::uint8_t { hand, draw, discard, exhaust, play, count };
enum class Phase : std::uint8_t { uninitialized, play, discard_choice, combat_won, player_died, card_choice, invalid };
enum class BeetleMove : std::uint8_t { shrink, chomp, stomp };
struct CardInstance {
    CardId definition = CardId::strike;
    bool upgraded = false;
    std::uint32_t creation_order = 0;
    std::uint32_t deck_instance = UINT32_MAX;
    bool removed = false;
    friend bool operator==(const CardInstance&, const CardInstance&) = default;
};
using CardPool = Pool<CardInstance, CardInstanceTag>;
struct CreatureState {
    std::int32_t hp = 0, max_hp = 0, block = 0, vulnerable = 0;
    bool shrink = false;
    std::int32_t strength = 0, dexterity = 0, weak = 0;
    friend bool operator==(const CreatureState&, const CreatureState&) = default;
};
// Every suspended operand is owned here, never on the C++ call stack or in Scratch.
struct EffectFrame {
    CardHandle source;
    std::uint32_t instruction = 0;
    std::uint32_t play = 0;
    friend bool operator==(const EffectFrame&, const EffectFrame&) = default;
};
enum class SelectionEffect : std::uint8_t { none, exhaust, upgrade, topdeck };
struct CardSelection {
    PromptId id;
    SelectionEffect effect = SelectionEffect::none;
    Zone zone = Zone::hand;
    std::uint32_t minimum = 0, maximum = 0;
    bool automatic_at_maximum = false;
    std::vector<CardHandle> candidates, selected;
    friend bool operator==(const CardSelection&, const CardSelection&) = default;
};
// Native AfterSideTurnEnd captures once; detached powers remain owned by index.
struct PowerDispatch {
    std::vector<std::uint32_t> candidates;
    std::uint32_t cursor = 0;
    bool active = false;
    friend bool operator==(const PowerDispatch&, const PowerDispatch&) = default;
};
struct State {
    std::uint32_t rules = rules_revision;
    BoundaryId boundary{};
    Phase phase = Phase::uninitialized;
    std::string seed;
    std::uint64_t numeric_seed = 0;
    RunClock clock;
    RngBank rng;
    CreatureState player, enemy;
    std::int32_t energy = 0, turn = 0;
    BeetleMove move = BeetleMove::shrink;
    bool burning_blood = true;
    CardPool cards;
    std::array<std::vector<CardHandle>, static_cast<std::size_t>(Zone::count)> zones;
    std::vector<EffectFrame> execution;
    CardSelection selection;
    PromptId next_prompt{1};
    std::vector<CombatHistoryEntry> history;
    std::vector<CardPlayRecord> plays;
    std::vector<PowerRecord> powers;
    PowerDispatch power_dispatch;
    std::int32_t damage_dealt = 0, damage_taken = 0, debuffs_applied = 0, healing = 0;
    std::uint32_t next_creation_order = 0;
    std::uint64_t cards_played = 0, cards_drawn = 0, shuffles = 0;
    [[nodiscard]] auto& zone(Zone id) noexcept { return zones[static_cast<std::size_t>(id)]; }
    [[nodiscard]] const auto& zone(Zone id) const noexcept { return zones[static_cast<std::size_t>(id)]; }
    void reserve(std::size_t card_count);
    [[nodiscard]] std::size_t retained_bytes() const noexcept;
    friend bool operator==(const State&, const State&) = default;
};
// Scratch is discardable and contains no continuation or authoritative values.
struct Scratch {
    std::uint32_t step_limit = 10000;
    std::uint32_t steps = 0;
};
struct Limits { std::uint32_t cards = 4096; };
struct CardSpec { CardId definition; bool upgraded = false; std::uint32_t deck_instance = UINT32_MAX; };
// Isolated combat setup. Seeded run integration supplies the generated RNG bank;
// a standalone scenario starts fresh streams and does not generate run content.
struct CombatConfig {
    std::string_view build = game_build;
    std::string_view seed = "ORACLECHECK01";
    ClockConfig clock;
    const RngBank* initial_rng = nullptr;
    std::span<const CardSpec> deck; // Empty selects the Ironclad starting deck.
    std::int32_t player_hp = 80;
    std::int32_t player_max_hp = 80;
    std::int32_t enemy_hp = 0; // Zero rolls the native A0 38..40 range.
    bool burning_blood = true;
    std::uint32_t card_generation = 1;
    std::uint8_t ascension = 0;
    std::string_view encounter = "ENCOUNTER.SHRINKER_BEETLE_WEAK";
};
} // namespace sts2::engine
