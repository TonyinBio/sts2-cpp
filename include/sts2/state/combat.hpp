#pragma once
#include <variant>
#include "sts2/core/result.hpp"
#include "sts2/core/storage.hpp"
#include "sts2/rules/ruleset.hpp"
#include "sts2/state/clock.hpp"
#include "sts2/state/rng.hpp"
#include "sts2/state/combat_types.hpp"
#include <array>
#include <algorithm>
#include <string_view>

namespace sts2::engine {
enum class Zone : std::uint8_t { hand, draw, discard, exhaust, play, count };
enum class CombatPhase : std::uint8_t { uninitialized, play, discard_choice, combat_won, player_died, card_choice, invalid };
enum class BeetleMove : std::uint8_t { shrink, chomp, stomp };
struct RememberedCardChoice {
    CardHandle card {};
    friend bool operator==(const RememberedCardChoice&, const RememberedCardChoice&) = default;
};
// Concrete typed exceptional properties, not an opaque fixed-size payload or
// universal modifier bag. Extend with audited types as content requires them.
using CardExtraState = std::variant<std::monostate, RememberedCardChoice>;
struct CardInstance {
    CardId definition = CardId::strike;
    bool upgraded = false;
    std::uint32_t creation_order = 0;
    PersistentCardHandle origin {};
    std::int32_t last_finished_player_turn = 0;
    bool removed = false;
    CardExtraState extra {};
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
    // Provisional inline ordered instances; this access contract does not select
    // a final dense/sparse layout. Capacity is a storage optimization, not a limit.
    InlineSequence<PowerInstance, 6> powers {};
    auto power_instances(OverflowStorage& owner) noexcept { return SequenceView(powers, owner); }
    auto power_instances(const OverflowStorage& owner) const noexcept { return SequenceView(powers, owner); }
    PowerInstance* find_power(PowerId id, OverflowStorage& owner) noexcept {
        for (auto& p : powers.values(owner)) if (p.definition == id && p.active) return &p;
        return nullptr;
    }
    const PowerInstance* find_power(PowerId id, const OverflowStorage& owner) const noexcept {
        for (const auto& p : powers.values(owner)) if (p.definition == id && p.active) return &p;
        return nullptr;
    }
    PowerInstance* find_power_instance(std::uint32_t id, OverflowStorage& owner) noexcept {
        for (auto& p : powers.values(owner)) if (p.instance == id && p.active) return &p;
        return nullptr;
    }
    bool has_power(PowerId id, const OverflowStorage& owner) const noexcept { return find_power(id, owner) != nullptr; }
    std::int32_t power_amount(PowerId id, const OverflowStorage& owner) const noexcept {
        std::int64_t total = 0;
        for (const auto& p : powers.values(owner)) if (p.definition == id && p.active) total += p.amount;
        return static_cast<std::int32_t>(std::clamp<std::int64_t>(total, INT32_MIN, INT32_MAX));
    }
    void clear_powers() noexcept { powers.clear(); }
    void assign_from(const CreatureState& other, OverflowStorage& owner) {
        auto retained = powers;
        *this = other; powers = retained;
        power_instances(owner).assign(other.power_instances(owner).begin(), other.power_instances(owner).end());
    }
    void clear() noexcept {
        auto retained = powers; *this = {}; powers = retained; powers.clear();
    }
    bool equals(const OverflowStorage& owner, const CreatureState& other, const OverflowStorage& other_owner) const {
        return hp == other.hp && max_hp == other.max_hp && block == other.block && positive_unblocked_hits == other.positive_unblocked_hits && powers.equals(owner, other.powers, other_owner);
    }
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
    CardHandle remember_to {};
    friend bool operator==(const ShuffleContinuation&, const ShuffleContinuation&) = default;
};
struct SelectionContinuation {
    SelectionEffect effect = SelectionEffect::none;
    Zone zone = Zone::hand;
    std::uint32_t begin = 0, count = 0, cursor = 0;
    CardHandle remember_to {};
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
    // Captured enemy operand, also for untargeted programs such as Shockwave.
    CreatureHandle target {};
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
    bool remember_choice = false;
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
    CreatureHandle player_identity {0, 1}, enemy_identity {1, 1};
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
    Owner& creature_overflow;
    using CreatureData = PoolData<CreatureState, CreatureTag, 8>;
    std::conditional_t<Const, const CreatureData, CreatureData>* creature_pool = nullptr;
    Owner* creature_pool_owner = nullptr;
    CreatureHandle player_handle {0, 1}, enemy_handle {1, 1};
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
    BasicCombatView(Data& data, Owner& owner, Ref<CreatureState> player, Ref<CreatureState> enemy, Ref<BoundaryId> boundary, Ref<RunClock> clock, Ref<RngBank> rng, Ref<std::uint32_t> joss, Owner* creature_owner = nullptr) noexcept : data(data), overflow(owner), creature_overflow(creature_owner ? *creature_owner : owner), boundary(boundary), clock(clock), rng(rng), player(player), enemy(enemy), joss_paper_counter(joss) {}
    BasicCombatView(Data& data, Owner& owner, Owner& creature_owner, std::conditional_t<Const, const CreatureData, CreatureData>& pool, CreatureHandle player_id, CreatureHandle enemy_id, Ref<BoundaryId> boundary, Ref<RunClock> clock, Ref<RngBank> rng, Ref<std::uint32_t> joss) noexcept : data(data), overflow(owner), creature_overflow(creature_owner), creature_pool(&pool), creature_pool_owner(&creature_owner), player_handle(player_id), enemy_handle(enemy_id), boundary(boundary), clock(clock), rng(rng), player(*PoolView<CreatureState, CreatureTag, 8, Const>{pool, creature_owner}.get(player_id)), enemy(*PoolView<CreatureState, CreatureTag, 8, Const>{pool, creature_owner}.get(enemy_id)), joss_paper_counter(joss) {}
    auto creature(CreatureHandle id) const noexcept -> std::conditional_t<Const, const CreatureState*, CreatureState*> {
        if (creature_pool) return PoolView<CreatureState, CreatureTag, 8, Const>{*creature_pool, creature_pool_owner ? *creature_pool_owner : creature_overflow}.get(id);
        return id == player_handle ? &player : id == enemy_handle ? &enemy : nullptr;
    }
    template<class Visit> void for_each_sequence(Visit visit) const noexcept {
        data.for_each_sequence(visit);
        if (&overflow == &creature_overflow) { visit(player.powers); visit(enemy.powers); }
    }
    struct CreatureStorageView {
        const CreatureState& player; const CreatureState& enemy;
        template<class Visit> void for_each_sequence(Visit visit) const noexcept { visit(player.powers); visit(enemy.powers); }
    };
    bool valid_storage() const noexcept {
        return valid_data_storage(*this, overflow) && player.powers.valid(creature_overflow) && enemy.powers.valid(creature_overflow) &&
            (creature_pool || &overflow == &creature_overflow || valid_data_storage(CreatureStorageView{player, enemy}, creature_overflow));
    }
    operator BasicCombatView<true>() const noexcept requires (!Const) {
        if (creature_pool) return {data, overflow, creature_overflow, *creature_pool, player_handle, enemy_handle, boundary, clock, rng, joss_paper_counter};
        auto result = BasicCombatView<true>{data, overflow, player, enemy, boundary, clock, rng, joss_paper_counter, &creature_overflow}; result.player_handle = player_handle; result.enemy_handle = enemy_handle; return result;
    }
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
        return player_handle == other.player_handle && enemy_handle == other.enemy_handle && rules == other.rules &&
            boundary == other.boundary &&
            phase == other.phase &&
            seed == other.seed &&
            numeric_seed == other.numeric_seed &&
            clock == other.clock &&
            rng == other.rng &&
            player.equals(creature_overflow, other.player, other.creature_overflow) &&
            enemy.equals(creature_overflow, other.enemy, other.creature_overflow) &&
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
    OverflowStorage overflow, creature_overflow;
    explicit CombatState(StorageAllocator allocator = {}) : overflow(allocator), creature_overflow(allocator) {}
    CombatState(const CombatState& other) : CombatState(other.overflow.allocator()) { if (clone_into(*this, other) != CloneStatus::cloned) throw std::bad_alloc{}; }
    explicit CombatState(ConstCombatView other) : CombatState(other.overflow.allocator()) {
        for (const auto& frame : other.execution)
            if (std::holds_alternative<std::monostate>(frame.pending) && frame.target != other.player_handle && frame.target != other.enemy_handle)
                throw std::invalid_argument("Standalone combat cannot retain targets outside active membership");
        for (const auto* creature : {&other.player, &other.enemy})
            for (const auto& power : creature->power_instances(other.creature_overflow))
                if (power.has_applier && power.applier != other.player_handle && power.applier != other.enemy_handle)
                    throw std::invalid_argument("Standalone combat cannot retain appliers outside active membership");
        overflow.clone_from(other.overflow); std::memcpy(&data, &other.data, sizeof(data)); player = other.player; enemy = other.enemy; boundary = other.boundary; clock = other.clock; rng = other.rng; joss_paper_counter = other.joss_paper_counter;
        player.powers = {}; enemy.powers = {};
        player.power_instances(creature_overflow).assign(other.player.power_instances(other.creature_overflow).begin(), other.player.power_instances(other.creature_overflow).end());
        enemy.power_instances(creature_overflow).assign(other.enemy.power_instances(other.creature_overflow).begin(), other.enemy.power_instances(other.creature_overflow).end());
        data.player_identity = other.player_handle; data.enemy_identity = other.enemy_handle;
    }
    CombatState& operator=(const CombatState& other) { if (clone_into(*this, other) != CloneStatus::cloned) throw std::bad_alloc{}; return *this; }
    CombatState(CombatState&& other) noexcept : player(other.player), enemy(other.enemy), boundary(other.boundary), clock(other.clock), rng(other.rng), joss_paper_counter(other.joss_paper_counter), overflow(std::move(other.overflow)), creature_overflow(std::move(other.creature_overflow)) { std::memcpy(&data, &other.data, sizeof(data)); }
    CombatState& operator=(CombatState&& other) noexcept {
        if (this != &other) { overflow = std::move(other.overflow); creature_overflow = std::move(other.creature_overflow); std::memcpy(&data, &other.data, sizeof(data)); player = other.player; enemy = other.enemy; boundary = other.boundary; clock = other.clock; rng = other.rng; joss_paper_counter = other.joss_paper_counter; }
        return *this;
    }
    void reset() noexcept { data = CombatData{}; player = {}; enemy = {}; boundary = {}; clock = {}; rng = RngBank{}; joss_paper_counter = 0; overflow.reset(); creature_overflow.reset(); }
    CombatView view() noexcept { auto result = CombatView{data, overflow, player, enemy, boundary, clock, rng, joss_paper_counter, &creature_overflow}; result.player_handle = data.player_identity; result.enemy_handle = data.enemy_identity; return result; }
    ConstCombatView view() const noexcept { auto result = ConstCombatView{data, overflow, player, enemy, boundary, clock, rng, joss_paper_counter, &creature_overflow}; result.player_handle = data.player_identity; result.enemy_handle = data.enemy_identity; return result; }
    operator CombatView() noexcept { return view(); }
    operator ConstCombatView() const noexcept { return view(); }
    auto zone(Zone id) noexcept { return view().zone(id); }
    auto zone(Zone id) const noexcept { return view().zone(id); }
    std::size_t retained_bytes() const noexcept { return sizeof(*this) + overflow.retained_bytes() + creature_overflow.retained_bytes(); }
    friend CloneStatus clone_into(CombatState& destination, const CombatState& source) noexcept try {
        if (&destination == &source) return CloneStatus::cloned;
        OverflowStorage next_overflow(destination.overflow.allocator()), next_creature_overflow(destination.creature_overflow.allocator());
        next_overflow.clone_from(source.overflow); next_creature_overflow.clone_from(source.creature_overflow);
        destination.overflow = std::move(next_overflow); destination.creature_overflow = std::move(next_creature_overflow);
        std::memcpy(&destination.data, &source.data, sizeof(CombatData));
        destination.player = source.player; destination.enemy = source.enemy; destination.boundary = source.boundary; destination.clock = source.clock; destination.rng = source.rng; destination.joss_paper_counter = source.joss_paper_counter;
        return CloneStatus::cloned;
    } catch (const std::bad_alloc&) { return CloneStatus::resource_limit; }
      catch (const std::length_error&) { return CloneStatus::resource_limit; }
    friend bool operator==(const CombatState& a, const CombatState& b) { return a.view() == b.view(); }
};
template<bool Const> void BasicCombatView<Const>::replace(CombatState&& source) requires (!Const) {
    if (creature_pool) {
        auto next_player = source.player, next_enemy = source.enemy;
        next_player.powers = player.powers; next_enemy.powers = enemy.powers;
        if (source.player.powers.overflowed() || source.enemy.powers.overflowed() || player.powers.overflowed() || enemy.powers.overflowed()) {
            // Prepare both owners before publishing; pool records themselves may
            // be in run overflow, so reacquire them after the atomic owner swap.
            OverflowStorage next_owner(creature_overflow.allocator());
            next_owner.clone_from(creature_overflow);
            next_player.power_instances(next_owner).assign(source.player.power_instances(source.creature_overflow).begin(), source.player.power_instances(source.creature_overflow).end());
            next_enemy.power_instances(next_owner).assign(source.enemy.power_instances(source.creature_overflow).begin(), source.enemy.power_instances(source.creature_overflow).end());
            creature_overflow.swap(next_owner);
        } else {
            next_player.powers = source.player.powers; next_enemy.powers = source.enemy.powers;
        }
        auto pool = PoolView<CreatureState, CreatureTag, 8>{*creature_pool, creature_overflow};
        *pool.get(player_handle) = next_player; *pool.get(enemy_handle) = next_enemy;
    } else {
        creature_overflow = std::move(source.creature_overflow); player = source.player; enemy = source.enemy;
    }
    overflow = std::move(source.overflow);
    std::memcpy(&data, &source.data, sizeof(data));
    boundary = source.boundary; clock = source.clock; rng = source.rng; joss_paper_counter = source.joss_paper_counter;
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
