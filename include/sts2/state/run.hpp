#pragma once
#include "sts2/state/setup.hpp"
#include "sts2/state/state.hpp"
#include <optional>

namespace sts2::engine {
enum class RunPhase : std::uint8_t { uninitialized, neow, neow_done, map, combat, combat_done, rewards, complete, invalid };
struct CombatRewards {
    std::int32_t gold = 0;
    ContentId potion = no_content;
    std::array<PersistentCard, 3> cards{};
    bool gold_taken : 1 = false;
    bool potion_taken : 1 = false;
    bool card_taken : 1 = false;
    bool selecting_card : 1 = false;
    bool resolved : 1 = false;
    bool skipped : 1 = false;
    std::uint8_t selected_card = UINT8_MAX;
    friend bool operator==(const CombatRewards&, const CombatRewards&) = default;
};
// Optional active combat is a trivial record, with ownership outside RunRecord.
template<bool Const> struct OptionalCombatView {
    using Record = std::conditional_t<Const, const std::optional<StateRecord>, std::optional<StateRecord>>;
    using Owner = std::conditional_t<Const, const OverflowStorage, OverflowStorage>;
    Record& record;
    Owner& overflow;
    bool has_value() const noexcept { return record.has_value(); }
    explicit operator bool() const noexcept { return has_value(); }
    auto operator*() const noexcept { return BasicStateView<Const>(*record, overflow); }
    auto operator->() const noexcept { return ViewArrow<BasicStateView<Const>>{**this}; }
    auto emplace() requires (!Const) { overflow.reset(); record.emplace(); return **this; }
    void reset() noexcept requires (!Const) { record.reset(); overflow.reset(); }
    OptionalCombatView& operator=(State&& state) requires (!Const) {
        record.emplace(); (**this).replace(std::move(state)); return *this;
    }
    OptionalCombatView& operator=(std::optional<State>&& state) requires (!Const) {
        if (state) *this = std::move(*state); else reset(); return *this;
    }
    template<bool Other> bool operator==(const OptionalCombatView<Other>& other) const {
        return has_value() == other.has_value() && (!has_value() || **this == *other);
    }
};
struct RunRecord {
    SetupRecord setup;
    std::optional<StateRecord> combat;
    bool direct_combat = false;
    std::uint32_t combat_generation = 0;
    std::optional<CombatRewards> rewards;
    RunClock clock;
    BoundaryId boundary{};
    RunPhase phase = RunPhase::uninitialized;
    std::uint8_t act_index = 0;
    std::array<std::optional<MapState>, 3> maps;
    MapNodeId current = no_node;
    InlineSequence<MapNodeId, 64> visited;
    GameRng event_rng;
    std::array<ContentId, 3> neow_options{};
    ContentId selected_neow = no_content;
    template<class Visit> void for_each_sequence(Visit visit) const noexcept { visit(visited); }
};
static_assert(std::is_trivially_copyable_v<RunRecord>);
template<bool Const> struct BasicRunView {
    using Record = std::conditional_t<Const, const RunRecord, RunRecord>;
    using Owner = std::conditional_t<Const, const OverflowStorage, OverflowStorage>;
    template<class T> using Ref = std::conditional_t<Const, const T&, T&>;
    Record& record;
    Owner& overflow;
    BasicSetupView<Const> setup;
    OptionalCombatView<Const> combat;
    Ref<bool> direct_combat = record.direct_combat;
    Ref<std::uint32_t> combat_generation = record.combat_generation;
    Ref<std::optional<CombatRewards>> rewards = record.rewards;
    Ref<RunClock> clock = record.clock;
    Ref<BoundaryId> boundary = record.boundary;
    Ref<RunPhase> phase = record.phase;
    Ref<std::uint8_t> act_index = record.act_index;
    Ref<std::array<std::optional<MapState>, 3>> maps = record.maps;
    Ref<MapNodeId> current = record.current;
    SequenceView<MapNodeId, 64, Const> visited{record.visited, overflow};
    Ref<GameRng> event_rng = record.event_rng;
    Ref<std::array<ContentId, 3>> neow_options = record.neow_options;
    Ref<ContentId> selected_neow = record.selected_neow;
    BasicRunView(Record& r, Owner& run_owner, Owner& setup_owner, Owner& combat_owner) noexcept
        : record(r), overflow(run_owner), setup(r.setup, setup_owner), combat{r.combat, combat_owner} {}
    BasicRunView view() const noexcept { return *this; }
    operator BasicRunView<true>() const noexcept requires (!Const) { return {record, overflow, setup.overflow, combat.overflow}; }
    template<bool Other> bool operator==(const BasicRunView<Other>& other) const {
        return setup == other.setup && combat == other.combat && direct_combat == other.direct_combat &&
            combat_generation == other.combat_generation && rewards == other.rewards && clock == other.clock &&
            boundary == other.boundary && phase == other.phase && act_index == other.act_index && maps == other.maps &&
            current == other.current && visited == other.visited && event_rng == other.event_rng &&
            neow_options == other.neow_options && selected_neow == other.selected_neow;
    }
};
using RunView = BasicRunView<false>;
using ConstRunView = BasicRunView<true>;
struct RunState;
CloneStatus clone_into(RunState&, const RunState&) noexcept;
struct RunState {
    RunRecord record;
    OverflowStorage overflow, setup_overflow, combat_overflow;
    explicit RunState(StorageAllocator allocator = {}) : overflow(allocator), setup_overflow(allocator), combat_overflow(allocator) {}
    RunState(const RunState& other) : RunState(other.overflow.allocator()) { if (clone_into(*this, other) != CloneStatus::cloned) throw std::bad_alloc{}; }
    RunState& operator=(const RunState& other) { if (clone_into(*this, other) != CloneStatus::cloned) throw std::bad_alloc{}; return *this; }
    RunState(RunState&& other) noexcept : overflow(std::move(other.overflow)), setup_overflow(std::move(other.setup_overflow)), combat_overflow(std::move(other.combat_overflow)) {
        std::memcpy(&record, &other.record, sizeof(record));
    }
    RunState& operator=(RunState&& other) noexcept {
        if (this != &other) {
            overflow = std::move(other.overflow); setup_overflow = std::move(other.setup_overflow); combat_overflow = std::move(other.combat_overflow);
            std::memcpy(&record, &other.record, sizeof(record));
        }
        return *this;
    }
    void reset() noexcept { record = RunRecord{}; overflow.reset(); setup_overflow.reset(); combat_overflow.reset(); }
    RunView view() noexcept { return {record, overflow, setup_overflow, combat_overflow}; }
    ConstRunView view() const noexcept { return {record, overflow, setup_overflow, combat_overflow}; }
    friend CloneStatus clone_into(RunState& destination, const RunState& source) noexcept try {
        if (&destination == &source) return CloneStatus::cloned;
        // Prepare all ownership before publishing any part of the complete run.
        OverflowStorage run(destination.overflow.allocator()), setup(destination.setup_overflow.allocator()), combat(destination.combat_overflow.allocator());
        run.clone_from(source.overflow); setup.clone_from(source.setup_overflow); combat.clone_from(source.combat_overflow);
        destination.overflow.swap(run); destination.setup_overflow.swap(setup); destination.combat_overflow.swap(combat);
        std::memcpy(&destination.record, &source.record, sizeof(RunRecord));
        return CloneStatus::cloned;
    } catch (const std::bad_alloc&) { return CloneStatus::resource_limit; }
      catch (const std::length_error&) { return CloneStatus::resource_limit; }
    friend bool operator==(const RunState& a, const RunState& b) { return a.view() == b.view(); }
};
} // namespace sts2::engine
