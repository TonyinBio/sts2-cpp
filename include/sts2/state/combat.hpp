#pragma once
#include "sts2/core/result.hpp"
#include "sts2/core/storage.hpp"
#include "sts2/rules/ruleset.hpp"
#include "sts2/state/clock.hpp"
#include "sts2/state/rng.hpp"
#include "sts2/state/history.hpp"
#include <array>
#include <string_view>

namespace sts2::engine {
enum class Zone : std::uint8_t { hand, draw, discard, exhaust, play, count };
enum class CombatPhase : std::uint8_t { uninitialized, play, discard_choice, combat_won, player_died, invalid };
enum class BeetleMove : std::uint8_t { shrink, chomp, stomp };
struct CardInstance {
    CardId definition = CardId::strike;
    bool upgraded = false;
    std::uint32_t creation_order = 0;
    std::uint32_t deck_instance = UINT32_MAX;
    friend bool operator==(const CardInstance&, const CardInstance&) = default;
};
using CardPool = PoolData<CardInstance, CardInstanceTag, 32>;
struct CreatureState {
    std::int32_t hp = 0, max_hp = 0, block = 0, vulnerable = 0;
    bool shrink = false;
    friend bool operator==(const CreatureState&, const CreatureState&) = default;
};
// Every suspended operand is owned here, never on the C++ call stack or in Scratch.
struct EffectFrame {
    CardHandle source;
    std::uint32_t instruction = 0;
    std::uint32_t play = 0;
    friend bool operator==(const EffectFrame&, const EffectFrame&) = default;
};
struct CombatData {
    std::uint32_t rules = rules_revision;
    BoundaryId boundary {};
    CombatPhase phase = CombatPhase::uninitialized;
    InlineSequence<char, 32> seed {};
    std::uint64_t numeric_seed = 0;
    RunClock clock {};
    RngBank rng {};
    CreatureState player {};
    CreatureState enemy {};
    std::int32_t energy = 0;
    std::int32_t turn = 0;
    BeetleMove move = BeetleMove::shrink;
    bool burning_blood = true;
    CardPool cards {};
    std::array<InlineSequence<CardHandle, 32>, static_cast<std::size_t>(Zone::count)> zones {};
    InlineSequence<EffectFrame, 4> execution {};
    InlineSequence<CombatHistoryEntry, 64> history {};
    InlineSequence<CardPlayRecord, 16> plays {};
    InlineSequence<PowerRecord, 8> powers {};
    std::uint32_t next_creation_order = 0;
    template<class Self, class Visit> static void visit_sequences(Self& r, Visit visit) noexcept {
        visit(r.seed); visit(r.cards.slots); visit(r.cards.free);
        for (auto& zone : r.zones) visit(zone);
        visit(r.execution); visit(r.history); visit(r.plays); visit(r.powers);
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
    Ref<BoundaryId> boundary = data.boundary;
    Ref<CombatPhase> phase = data.phase;
    TextView<32, Const> seed{data.seed, overflow};
    Ref<std::uint64_t> numeric_seed = data.numeric_seed;
    Ref<RunClock> clock = data.clock;
    Ref<RngBank> rng = data.rng;
    Ref<CreatureState> player = data.player;
    Ref<CreatureState> enemy = data.enemy;
    Ref<std::int32_t> energy = data.energy;
    Ref<std::int32_t> turn = data.turn;
    Ref<BeetleMove> move = data.move;
    Ref<bool> burning_blood = data.burning_blood;
    PoolView<CardInstance, CardInstanceTag, 32, Const> cards{data.cards, overflow};
    SequenceView<EffectFrame, 4, Const> execution{data.execution, overflow};
    SequenceView<CombatHistoryEntry, 64, Const> history{data.history, overflow};
    SequenceView<CardPlayRecord, 16, Const> plays{data.plays, overflow};
    SequenceView<PowerRecord, 8, Const> powers{data.powers, overflow};
    Ref<std::uint32_t> next_creation_order = data.next_creation_order;
    BasicCombatView(Data& data, Owner& owner) noexcept : data(data), overflow(owner) {}
    operator BasicCombatView<true>() const noexcept requires (!Const) { return {data, overflow}; }
    BasicCombatView view() const noexcept { return *this; }
    auto zone(Zone id) const noexcept { return SequenceView<CardHandle, 32, Const>(data.zones[static_cast<std::size_t>(id)], overflow); }
    auto zone_views() const noexcept { return std::array<SequenceView<CardHandle, 32, Const>, 5>{{{data.zones[0], overflow}, {data.zones[1], overflow}, {data.zones[2], overflow}, {data.zones[3], overflow}, {data.zones[4], overflow}}}; }
    void reserve(std::size_t card_count) requires (!Const) {
        cards.reserve(card_count);
        for (auto zone : zone_views()) zone.reserve(card_count);
    }
    std::size_t retained_bytes() const noexcept { return sizeof(CombatData) + overflow.retained_bytes(); }
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
            cards == other.cards &&
            zone_views() == other.zone_views() &&
            execution == other.execution &&
            history == other.history &&
            plays == other.plays &&
            powers == other.powers &&
            next_creation_order == other.next_creation_order;
    }
};
using CombatView = BasicCombatView<false>;
using ConstCombatView = BasicCombatView<true>;
struct CombatState {
    CombatData data;
    OverflowStorage overflow;
    explicit CombatState(StorageAllocator allocator = {}) : overflow(allocator) {}
    CombatState(const CombatState& other) : CombatState(other.overflow.allocator()) { if (clone_into(*this, other) != CloneStatus::cloned) throw std::bad_alloc{}; }
    explicit CombatState(ConstCombatView other) : CombatState(other.overflow.allocator()) {
        overflow.clone_from(other.overflow); std::memcpy(&data, &other.data, sizeof(data));
    }
    CombatState& operator=(const CombatState& other) { if (clone_into(*this, other) != CloneStatus::cloned) throw std::bad_alloc{}; return *this; }
    CombatState(CombatState&& other) noexcept : overflow(std::move(other.overflow)) { std::memcpy(&data, &other.data, sizeof(data)); }
    CombatState& operator=(CombatState&& other) noexcept {
        if (this != &other) { overflow = std::move(other.overflow); std::memcpy(&data, &other.data, sizeof(data)); }
        return *this;
    }
    void reset() noexcept { data = CombatData{}; overflow.reset(); }
    CombatView view() noexcept { return {data, overflow}; }
    ConstCombatView view() const noexcept { return {data, overflow}; }
    operator CombatView() noexcept { return view(); }
    operator ConstCombatView() const noexcept { return view(); }
    auto zone(Zone id) noexcept { return view().zone(id); }
    auto zone(Zone id) const noexcept { return view().zone(id); }
    std::size_t retained_bytes() const noexcept { return sizeof(*this) + overflow.retained_bytes(); }
    friend CloneStatus clone_into(CombatState& destination, const CombatState& source) noexcept try {
        if (&destination == &source) return CloneStatus::cloned;
        destination.overflow.clone_from(source.overflow);
        std::memcpy(&destination.data, &source.data, sizeof(CombatData));
        return CloneStatus::cloned;
    } catch (const std::bad_alloc&) { return CloneStatus::resource_limit; }
      catch (const std::length_error&) { return CloneStatus::resource_limit; }
    friend bool operator==(const CombatState& a, const CombatState& b) { return a.view() == b.view(); }
};
template<bool Const> void BasicCombatView<Const>::replace(CombatState&& source) requires (!Const) {
    overflow = std::move(source.overflow);
    std::memcpy(&data, &source.data, sizeof(data));
}
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
