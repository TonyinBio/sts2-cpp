#pragma once
#include <variant>
#include "sts2/core/result.hpp"
#include "sts2/core/storage.hpp"
#include "sts2/rules/ruleset.hpp"
#include "sts2/state/clock.hpp"
#include "sts2/state/rng.hpp"
#include "sts2/state/combat_types.hpp"
#include <array>
#include <string_view>

namespace sts2::engine {
enum class Zone : std::uint8_t { hand, draw, discard, exhaust, play, count };
enum class CombatPhase : std::uint8_t { uninitialized, play, discard_choice, combat_won, player_died, card_choice, invalid };
enum class BeetleMove : std::uint8_t { shrink, chomp, stomp };
struct CardInstance {
    CardId definition = CardId::strike;
    bool upgraded = false;
    std::uint32_t creation_order = 0;
    PersistentCardHandle origin {};
    std::int32_t last_finished_player_turn = 0;
    bool removed = false;
    friend bool operator==(const CardInstance&, const CardInstance&) = default;
};
enum class CostModifierKind : std::uint8_t { absolute, relative };
enum class CostExpiry : std::uint8_t { combat = 0, turn = 2, played = 4, turn_or_played = 6 };
struct CostModifier {
    CardHandle card {};
    CostModifierKind kind = CostModifierKind::relative;
    CostExpiry expiry = CostExpiry::combat;
    std::int32_t amount = 0;
    bool reduce_only = false;
    friend bool operator==(const CostModifier&, const CostModifier&) = default;
};
using CardPool = PoolData<CardInstance, CardInstanceTag, 32>;
struct CreatureState {
    std::int32_t hp = 0, max_hp = 0, block = 0;
    std::uint64_t positive_unblocked_hits = 0;
    // Only implemented types have slots. No separate cached effect values.
    static constexpr std::size_t power_type_count = static_cast<std::size_t>(PowerId::count);
    std::array<PowerInstance, power_type_count> powers {};
    std::array<PowerId, power_type_count> power_order {};
    std::uint16_t power_count = 0;
    PowerInstance& power(PowerId id) noexcept { return powers[static_cast<std::size_t>(id)]; }
    const PowerInstance& power(PowerId id) const noexcept { return powers[static_cast<std::size_t>(id)]; }
    bool has_power(PowerId id) const noexcept { return power(id).active; }
    std::int32_t power_amount(PowerId id) const noexcept { return power(id).amount; }
    std::span<const PowerId> power_ids() const noexcept { return {power_order.data(), power_count}; }
    void clear_powers() noexcept { powers = {}; power_order = {}; power_count = 0; }
    friend bool operator==(const CreatureState&, const CreatureState&) = default;
};
// Every suspended operand is owned here, never on the C++ call stack or in Scratch.
enum class SelectionEffect : std::uint8_t { none, exhaust, upgrade, topdeck };
enum class ExhaustRelic : std::uint8_t { charons_ashes, joss_paper };
struct DrawContinuation {
    std::int32_t remaining = 0;
    bool from_hand_draw = true;
    friend bool operator==(const DrawContinuation&, const DrawContinuation&) = default;
};
struct ShuffleContinuation {
    std::uint32_t begin = 0, count = 0, cursor = 0;
    bool waiting = false;
    friend bool operator==(const ShuffleContinuation&, const ShuffleContinuation&) = default;
};
struct SelectionContinuation {
    SelectionEffect effect = SelectionEffect::none;
    Zone zone = Zone::hand;
    std::uint32_t begin = 0, count = 0, cursor = 0;
    friend bool operator==(const SelectionContinuation&, const SelectionContinuation&) = default;
};
struct ExhaustContinuation {
    std::array<ExhaustRelic, 2> candidates {};
    std::uint8_t count = 0, cursor = 0;
    friend bool operator==(const ExhaustContinuation&, const ExhaustContinuation&) = default;
};
struct RelicDrawCompletion { friend bool operator==(const RelicDrawCompletion&, const RelicDrawCompletion&) = default; };
using ContinuationPayload = std::variant<std::monostate, DrawContinuation, ShuffleContinuation, SelectionContinuation, ExhaustContinuation, RelicDrawCompletion>;
struct EffectFrame {
    CardHandle source;
    std::uint32_t instruction = 0;
    std::uint32_t play = 0;
    std::int32_t energy = 0;
    bool enemy_target = false;
    ContinuationPayload pending {};
    friend bool operator==(const EffectFrame&, const EffectFrame&) = default;
};
static_assert(std::is_trivially_copyable_v<EffectFrame>);
struct CardSelection {
    PromptId id {};
    SelectionEffect effect = SelectionEffect::none;
    Zone zone = Zone::hand;
    std::uint32_t minimum = 0, maximum = 0;
    bool automatic_at_maximum = false;
    std::uint32_t continuation_depth = 0;
    friend bool operator==(const CardSelection&, const CardSelection&) = default;
};
// These compiled reactions are used by controlled architecture fixtures only;
// their presence does not establish native content support.
enum class ShuffleReactionKind : std::uint8_t { choose_card, nested_draw };
struct ShuffleReaction {
    ShuffleReactionKind kind = ShuffleReactionKind::choose_card;
    bool fired = false;
    friend bool operator==(const ShuffleReaction&, const ShuffleReaction&) = default;
};
using ShuffleReactionPool = PoolData<ShuffleReaction, ShuffleReactionTag, 4>;
// Sufficient summaries for the audited single-player consumers. Card types are
// immutable in supported content; mutable-type mechanics need a fresh audit.
struct CombatCounters {
    std::uint32_t started_plays = 0, finished_plays = 0;
    std::uint32_t started_this_turn = 0, attacks_this_turn = 0;
    std::uint32_t zero_energy_attacks_this_turn = 0, non_hand_draws_this_turn = 0;
    std::uint32_t finished_ethereal = 0;
    friend bool operator==(const CombatCounters&, const CombatCounters&) = default;
};
struct EnemyAiState {
    // Shrinker's deterministic cycle needs only its selected move, not a
    // duplicate intent or invented history. Other AI can add required records.
    BeetleMove selected = BeetleMove::shrink;
};
struct CombatData {
    std::uint32_t rules = rules_revision;
    CombatPhase phase = CombatPhase::uninitialized;
    InlineSequence<char, 32> seed {};
    std::uint64_t numeric_seed = 0;
    std::int32_t energy = 0;
    std::int32_t turn = 0;
    EnemyAiState enemy_ai {};
    bool burning_blood = true;
    CardPool cards {};
    InlineSequence<CostModifier, 8> cost_modifiers {};
    std::array<InlineSequence<CardHandle, 32>, static_cast<std::size_t>(Zone::count)> zones {};
    InlineSequence<EffectFrame, 4> execution {};
    CombatCounters counters {};
    std::array<ExhaustRelic, 2> exhaust_relics {};
    std::uint8_t exhaust_relic_count = 0;
    ShuffleReactionPool shuffle_reactions {};
    InlineSequence<ShuffleReactionHandle, 4> shuffle_order {};
    InlineSequence<ShuffleReactionHandle, 16> captured_shuffle_reactions {};
    InlineSequence<CardHandle, 16> continuation_cards {};
    CardSelection selection {};
    InlineSequence<CardHandle, 10> selection_candidates {}, selection_order {};
    PromptId next_prompt {1};
    std::uint32_t next_power_instance = 0;
    std::uint32_t next_creation_order = 0;
    template<class Self, class Visit> static void visit_sequences(Self& r, Visit visit) noexcept {
        visit(r.seed); visit(r.cards.slots); visit(r.cards.free); visit(r.cost_modifiers);
        for (auto& zone : r.zones) visit(zone);
        visit(r.execution); visit(r.selection_candidates); visit(r.selection_order);
        visit(r.shuffle_reactions.slots); visit(r.shuffle_reactions.free); visit(r.shuffle_order);
        visit(r.captured_shuffle_reactions); visit(r.continuation_cards);
    }
    template<class Visit> void for_each_sequence(Visit visit) const noexcept { visit_sequences(*this, visit); }
    template<class Visit> void for_each_sequence(Visit visit) noexcept { visit_sequences(*this, visit); }
};
static_assert(std::is_trivially_copyable_v<CombatData>);
struct CombatState;
CloneStatus clone_into(CombatState&, const CombatState&) noexcept;
template<bool Const> struct BasicCombatView {
    using Data = std::conditional_t<Const, const CombatData, CombatData>;
    using Owner = std::conditional_t<Const, const OverflowStorage, OverflowStorage>;
    template<class T> using Ref = std::conditional_t<Const, const T&, T&>;
    Data& data;
    Owner& overflow;
    Ref<std::uint32_t> rules = data.rules;
    Ref<BoundaryId> boundary;
    Ref<CombatPhase> phase = data.phase;
    TextView<32, Const> seed{data.seed, overflow};
    Ref<std::uint64_t> numeric_seed = data.numeric_seed;
    Ref<RunClock> clock;
    Ref<RngBank> rng;
    Ref<CreatureState> player;
    Ref<CreatureState> enemy;
    Ref<std::int32_t> energy = data.energy;
    Ref<std::int32_t> turn = data.turn;
    Ref<BeetleMove> move = data.enemy_ai.selected;
    Ref<bool> burning_blood = data.burning_blood;
    PoolView<CardInstance, CardInstanceTag, 32, Const> cards{data.cards, overflow};
    SequenceView<CostModifier, 8, Const> cost_modifiers{data.cost_modifiers, overflow};
    SequenceView<EffectFrame, 4, Const> execution{data.execution, overflow};
    Ref<CombatCounters> counters = data.counters;
    Ref<std::array<ExhaustRelic, 2>> exhaust_relics = data.exhaust_relics;
    Ref<std::uint8_t> exhaust_relic_count = data.exhaust_relic_count;
    Ref<std::uint32_t> joss_paper_counter;
    PoolView<ShuffleReaction, ShuffleReactionTag, 4, Const> shuffle_reactions{data.shuffle_reactions, overflow};
    SequenceView<ShuffleReactionHandle, 4, Const> shuffle_order{data.shuffle_order, overflow};
    SequenceView<ShuffleReactionHandle, 16, Const> captured_shuffle_reactions{data.captured_shuffle_reactions, overflow};
    SequenceView<CardHandle, 16, Const> continuation_cards{data.continuation_cards, overflow};
    Ref<CardSelection> selection = data.selection;
    SequenceView<CardHandle, 10, Const> selection_candidates{data.selection_candidates, overflow};
    SequenceView<CardHandle, 10, Const> selection_order{data.selection_order, overflow};
    Ref<PromptId> next_prompt = data.next_prompt;
    Ref<std::uint32_t> next_power_instance = data.next_power_instance;
    Ref<std::uint32_t> next_creation_order = data.next_creation_order;
    BasicCombatView(Data& data, Owner& owner, Ref<CreatureState> player, Ref<CreatureState> enemy, Ref<BoundaryId> boundary, Ref<RunClock> clock, Ref<RngBank> rng, Ref<std::uint32_t> joss) noexcept : data(data), overflow(owner), boundary(boundary), clock(clock), rng(rng), player(player), enemy(enemy), joss_paper_counter(joss) {}
    operator BasicCombatView<true>() const noexcept requires (!Const) { return {data, overflow, player, enemy, boundary, clock, rng, joss_paper_counter}; }
    BasicCombatView view() const noexcept { return *this; }
    auto zone(Zone id) const noexcept { return SequenceView<CardHandle, 32, Const>(data.zones[static_cast<std::size_t>(id)], overflow); }
    auto zone_views() const noexcept { return std::array<SequenceView<CardHandle, 32, Const>, 5>{{{data.zones[0], overflow}, {data.zones[1], overflow}, {data.zones[2], overflow}, {data.zones[3], overflow}, {data.zones[4], overflow}}}; }
    void reserve(std::size_t card_count) requires (!Const) {
        cards.reserve(card_count);
        for (auto zone : zone_views()) zone.reserve(card_count);
    }
    std::size_t retained_bytes() const noexcept { return sizeof(CombatData) + 2 * sizeof(CreatureState) + sizeof(BoundaryId) + sizeof(RunClock) + sizeof(RngBank) + sizeof(std::uint32_t) + overflow.retained_bytes(); }
    void replace(CombatState&& source) requires (!Const);
    template<bool Other> bool operator==(const BasicCombatView<Other>& other) const {
        return rules == other.rules &&
            boundary == other.boundary &&
            phase == other.phase &&
            seed == other.seed &&
            numeric_seed == other.numeric_seed &&
            clock == other.clock &&
            rng == other.rng &&
            player == other.player &&
            enemy == other.enemy &&
            energy == other.energy &&
            turn == other.turn &&
            move == other.move &&
            burning_blood == other.burning_blood &&
            cards == other.cards && cost_modifiers == other.cost_modifiers &&
            zone_views() == other.zone_views() &&
            execution == other.execution &&
            counters == other.counters && exhaust_relics == other.exhaust_relics &&
            exhaust_relic_count == other.exhaust_relic_count && joss_paper_counter == other.joss_paper_counter &&
            shuffle_reactions == other.shuffle_reactions && shuffle_order == other.shuffle_order &&
            captured_shuffle_reactions == other.captured_shuffle_reactions && continuation_cards == other.continuation_cards &&
            selection == other.selection && selection_candidates == other.selection_candidates &&
            selection_order == other.selection_order && next_prompt == other.next_prompt &&
            next_power_instance == other.next_power_instance &&
            next_creation_order == other.next_creation_order;
    }
};
using CombatView = BasicCombatView<false>;
using ConstCombatView = BasicCombatView<true>;
struct CombatState {
    CombatData data;
    CreatureState player {}, enemy {};
    BoundaryId boundary {};
    RunClock clock {};
    RngBank rng {};
    std::uint32_t joss_paper_counter = 0;
    OverflowStorage overflow;
    explicit CombatState(StorageAllocator allocator = {}) : overflow(allocator) {}
    CombatState(const CombatState& other) : CombatState(other.overflow.allocator()) { if (clone_into(*this, other) != CloneStatus::cloned) throw std::bad_alloc{}; }
    explicit CombatState(ConstCombatView other) : CombatState(other.overflow.allocator()) {
        overflow.clone_from(other.overflow); std::memcpy(&data, &other.data, sizeof(data)); player = other.player; enemy = other.enemy; boundary = other.boundary; clock = other.clock; rng = other.rng; joss_paper_counter = other.joss_paper_counter;
    }
    CombatState& operator=(const CombatState& other) { if (clone_into(*this, other) != CloneStatus::cloned) throw std::bad_alloc{}; return *this; }
    CombatState(CombatState&& other) noexcept : player(other.player), enemy(other.enemy), boundary(other.boundary), clock(other.clock), rng(other.rng), joss_paper_counter(other.joss_paper_counter), overflow(std::move(other.overflow)) { std::memcpy(&data, &other.data, sizeof(data)); }
    CombatState& operator=(CombatState&& other) noexcept {
        if (this != &other) { overflow = std::move(other.overflow); std::memcpy(&data, &other.data, sizeof(data)); player = other.player; enemy = other.enemy; boundary = other.boundary; clock = other.clock; rng = other.rng; joss_paper_counter = other.joss_paper_counter; }
        return *this;
    }
    void reset() noexcept { data = CombatData{}; player = {}; enemy = {}; boundary = {}; clock = {}; rng = RngBank{}; joss_paper_counter = 0; overflow.reset(); }
    CombatView view() noexcept { return {data, overflow, player, enemy, boundary, clock, rng, joss_paper_counter}; }
    ConstCombatView view() const noexcept { return {data, overflow, player, enemy, boundary, clock, rng, joss_paper_counter}; }
    operator CombatView() noexcept { return view(); }
    operator ConstCombatView() const noexcept { return view(); }
    auto zone(Zone id) noexcept { return view().zone(id); }
    auto zone(Zone id) const noexcept { return view().zone(id); }
    std::size_t retained_bytes() const noexcept { return sizeof(*this) + overflow.retained_bytes(); }
    friend CloneStatus clone_into(CombatState& destination, const CombatState& source) noexcept try {
        if (&destination == &source) return CloneStatus::cloned;
        destination.overflow.clone_from(source.overflow);
        std::memcpy(&destination.data, &source.data, sizeof(CombatData));
        destination.player = source.player; destination.enemy = source.enemy; destination.boundary = source.boundary; destination.clock = source.clock; destination.rng = source.rng; destination.joss_paper_counter = source.joss_paper_counter;
        return CloneStatus::cloned;
    } catch (const std::bad_alloc&) { return CloneStatus::resource_limit; }
      catch (const std::length_error&) { return CloneStatus::resource_limit; }
    friend bool operator==(const CombatState& a, const CombatState& b) { return a.view() == b.view(); }
};
template<bool Const> void BasicCombatView<Const>::replace(CombatState&& source) requires (!Const) {
    overflow = std::move(source.overflow);
    std::memcpy(&data, &source.data, sizeof(data));
    player = source.player; enemy = source.enemy; boundary = source.boundary; clock = source.clock; rng = source.rng; joss_paper_counter = source.joss_paper_counter;
}
// Optional synchronous diagnostic output. No pointer is stored in cloned state.
// The receiver may allocate; exceptions stop execution through the normal error path.
struct CombatTrace {
    void* context = nullptr;
    void (*receive)(void*, ConstCombatView, const CombatEvent&) = nullptr;
    void emit(ConstCombatView state, const CombatEvent& event) const {
        if (receive) receive(context, state, event);
    }
};
// Scratch is discardable and contains no continuation or authoritative values.
// trace is optional instrumentation supplied by the caller for this execution.
struct Scratch {
    std::uint32_t step_limit = 10000;
    std::uint32_t steps = 0;
    CombatTrace trace {};
};
struct Limits { std::uint32_t cards = 4096; std::uint32_t continuations = 4096; };
struct CardSpec { CardId definition; bool upgraded = false; PersistentCardHandle origin {}; };
// Isolated combat setup. Seeded run integration supplies the generated RNG bank;
// a standalone scenario starts fresh streams and does not generate run content.
struct CombatConfig {
    std::span<const ExhaustRelic> exhaust_relics;
    std::uint32_t joss_paper_counter = 0;
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
