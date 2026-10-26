#pragma once
#include "sts2/rules/setup.hpp"
#include "sts2/state/map.hpp"
#include "sts2/state/clock.hpp"
#include "sts2/core/result.hpp"
#include "sts2/core/sequence_view.hpp"

namespace sts2::engine {
struct PlayerRngBank {
    std::array<GameRng, 3> streams;
    explicit PlayerRngBank(std::string_view seed = {}) noexcept;
    friend bool operator==(const PlayerRngBank&, const PlayerRngBank&) = default;
};
struct RelicBagRecord {
    std::array<InlineSequence<ContentId, 64>, 8> tiers;
    InlineSequence<RelicTier, 8> order;
};
template<bool Const> struct BasicRelicBagView {
    using Record = std::conditional_t<Const, const RelicBagRecord, RelicBagRecord>;
    using Owner = std::conditional_t<Const, const OverflowStorage, OverflowStorage>;
    Record& record;
    Owner& overflow;
    std::array<SequenceView<ContentId, 64, Const>, 8> tiers;
    SequenceView<RelicTier, 8, Const> order;
    BasicRelicBagView(Record& r, Owner& o) noexcept : record(r), overflow(o),
        tiers{{{r.tiers[0], o}, {r.tiers[1], o}, {r.tiers[2], o}, {r.tiers[3], o}, {r.tiers[4], o}, {r.tiers[5], o}, {r.tiers[6], o}, {r.tiers[7], o}}}, order(r.order, o) {}
    template<bool Other> bool operator==(const BasicRelicBagView<Other>& other) const { return tiers == other.tiers && order == other.order; }
};
using RelicBag = BasicRelicBagView<false>;
using ConstRelicBag = BasicRelicBagView<true>;
struct ActRoomsRecord {
    ContentId act = no_content, ancient = no_content, boss = no_content, second_boss = no_content;
    InlineSequence<ContentId, 32> events, normal, elites, shared_ancients;
    std::uint32_t events_visited = 0, normal_visited = 0, elites_visited = 0, bosses_visited = 0;
};
template<bool Const> struct BasicActRoomsView {
    template<class T> using Ref = std::conditional_t<Const, const T&, T&>;
    using Record = std::conditional_t<Const, const ActRoomsRecord, ActRoomsRecord>;
    using Owner = std::conditional_t<Const, const OverflowStorage, OverflowStorage>;
    Record& record;
    Owner& overflow;
    Ref<ContentId> act = record.act;
    Ref<ContentId> ancient = record.ancient;
    Ref<ContentId> boss = record.boss;
    Ref<ContentId> second_boss = record.second_boss;
    SequenceView<ContentId, 32, Const> events{record.events, overflow};
    SequenceView<ContentId, 32, Const> normal{record.normal, overflow};
    SequenceView<ContentId, 32, Const> elites{record.elites, overflow};
    SequenceView<ContentId, 32, Const> shared_ancients{record.shared_ancients, overflow};
    Ref<std::uint32_t> events_visited = record.events_visited;
    Ref<std::uint32_t> normal_visited = record.normal_visited;
    Ref<std::uint32_t> elites_visited = record.elites_visited;
    Ref<std::uint32_t> bosses_visited = record.bosses_visited;
    BasicActRoomsView(Record& r, Owner& o) noexcept : record(r), overflow(o) {}
    template<bool Other> bool operator==(const BasicActRoomsView<Other>& other) const { return act == other.act && ancient == other.ancient && boss == other.boss && second_boss == other.second_boss && events == other.events && normal == other.normal && elites == other.elites && shared_ancients == other.shared_ancients && events_visited == other.events_visited && normal_visited == other.normal_visited && elites_visited == other.elites_visited && bosses_visited == other.bosses_visited; }
};
struct PersistentCard {
    ContentId definition = no_content;
    std::uint32_t instance = 0;
    std::uint32_t upgrade = 0;
    std::int32_t floor_added = 1;
    friend bool operator==(const PersistentCard&, const PersistentCard&) = default;
};
struct SetupRecord {
    InlineSequence<char, 32> seed {};
    std::uint64_t numeric_seed = 0;
    ContentId character = no_content;
    std::uint8_t ascension = 0;
    std::int32_t hp = 0;
    std::int32_t max_hp = 0;
    std::int32_t gold = 0;
    std::int32_t potion_slots = 3;
    std::int32_t orb_slots = 0;
    RngBank rng {};
    PlayerRngBank player_rng {};
    RelicBagRecord shared_relics {};
    RelicBagRecord player_relics {};
    std::array<ActRoomsRecord, 3> acts {};
    InlineSequence<PersistentCard, 32> deck {};
    InlineSequence<ContentId, 16> relics {};
    InlineSequence<ContentId, 4> potions {};
    float card_rarity_offset = -0.05f;
    float potion_reward_odds = 0.4f;
    template<class Visit> void for_each_sequence(Visit visit) const noexcept {
        visit(seed);
        for (const auto* bag : {&shared_relics, &player_relics}) {
            for (const auto& tier : bag->tiers) visit(tier);
            visit(bag->order);
        }
        for (const auto& act : acts) { visit(act.events); visit(act.normal); visit(act.elites); visit(act.shared_ancients); }
        visit(deck); visit(relics); visit(potions);
    }
};
static_assert(std::is_trivially_copyable_v<SetupRecord>);
struct SetupState;
CloneStatus clone_into(SetupState&, const SetupState&) noexcept;
template<bool Const> struct BasicSetupView {
    using Record = std::conditional_t<Const, const SetupRecord, SetupRecord>;
    using Owner = std::conditional_t<Const, const OverflowStorage, OverflowStorage>;
    template<class T> using Ref = std::conditional_t<Const, const T&, T&>;
    Record& record;
    Owner& overflow;
    TextView<32, Const> seed{record.seed, overflow};
    Ref<std::uint64_t> numeric_seed = record.numeric_seed;
    Ref<ContentId> character = record.character;
    Ref<std::uint8_t> ascension = record.ascension;
    Ref<std::int32_t> hp = record.hp;
    Ref<std::int32_t> max_hp = record.max_hp;
    Ref<std::int32_t> gold = record.gold;
    Ref<std::int32_t> potion_slots = record.potion_slots;
    Ref<std::int32_t> orb_slots = record.orb_slots;
    Ref<RngBank> rng = record.rng;
    Ref<PlayerRngBank> player_rng = record.player_rng;
    BasicRelicBagView<Const> shared_relics{record.shared_relics, overflow};
    BasicRelicBagView<Const> player_relics{record.player_relics, overflow};
    std::array<BasicActRoomsView<Const>, 3> acts{{{record.acts[0], overflow}, {record.acts[1], overflow}, {record.acts[2], overflow}}};
    SequenceView<PersistentCard, 32, Const> deck{record.deck, overflow};
    SequenceView<ContentId, 16, Const> relics{record.relics, overflow};
    SequenceView<ContentId, 4, Const> potions{record.potions, overflow};
    Ref<float> card_rarity_offset = record.card_rarity_offset;
    Ref<float> potion_reward_odds = record.potion_reward_odds;
    BasicSetupView(Record& r, Owner& o) noexcept : record(r), overflow(o) {}
    operator BasicSetupView<true>() const noexcept requires (!Const) { return {record, overflow}; }
    BasicSetupView view() const noexcept { return *this; }
    void replace(SetupState&& source) requires (!Const);
    template<bool Other> bool operator==(const BasicSetupView<Other>& other) const { return seed == other.seed &&
        numeric_seed == other.numeric_seed &&
        character == other.character &&
        ascension == other.ascension &&
        hp == other.hp &&
        max_hp == other.max_hp &&
        gold == other.gold &&
        potion_slots == other.potion_slots &&
        orb_slots == other.orb_slots &&
        rng == other.rng &&
        player_rng == other.player_rng &&
        shared_relics == other.shared_relics &&
        player_relics == other.player_relics &&
        acts == other.acts &&
        deck == other.deck &&
        relics == other.relics &&
        potions == other.potions &&
        card_rarity_offset == other.card_rarity_offset &&
        potion_reward_odds == other.potion_reward_odds; }
};
using SetupView = BasicSetupView<false>;
using ConstSetupView = BasicSetupView<true>;
struct SetupState {
    SetupRecord record;
    OverflowStorage overflow;
    explicit SetupState(StorageAllocator allocator = {}) : overflow(allocator) {}
    SetupState(const SetupState& other) : SetupState(other.overflow.allocator()) { if (clone_into(*this, other) != CloneStatus::cloned) throw std::bad_alloc{}; }
    SetupState(ConstSetupView other) : SetupState(other.overflow.allocator()) { overflow.clone_from(other.overflow); std::memcpy(&record, &other.record, sizeof(record)); }
    SetupState(SetupView other) : SetupState(ConstSetupView(other)) {}
    SetupState& operator=(const SetupState& other) { if (clone_into(*this, other) != CloneStatus::cloned) throw std::bad_alloc{}; return *this; }
    SetupState(SetupState&& other) noexcept : overflow(std::move(other.overflow)) { std::memcpy(&record, &other.record, sizeof(record)); }
    SetupState& operator=(SetupState&& other) noexcept {
        if (this != &other) { overflow = std::move(other.overflow); std::memcpy(&record, &other.record, sizeof(record)); }
        return *this;
    }
    void reset() noexcept { record = SetupRecord{}; overflow.reset(); }
    SetupView view() noexcept { return {record, overflow}; }
    ConstSetupView view() const noexcept { return {record, overflow}; }
    operator SetupView() noexcept { return view(); }
    operator ConstSetupView() const noexcept { return view(); }
    friend CloneStatus clone_into(SetupState& destination, const SetupState& source) noexcept try {
        if (&destination == &source) return CloneStatus::cloned;
        destination.overflow.clone_from(source.overflow); std::memcpy(&destination.record, &source.record, sizeof(SetupRecord));
        return CloneStatus::cloned;
    } catch (const std::bad_alloc&) { return CloneStatus::resource_limit; }
      catch (const std::length_error&) { return CloneStatus::resource_limit; }
    friend bool operator==(const SetupState& a, const SetupState& b) { return a.view() == b.view(); }
};
template<bool Const> void BasicSetupView<Const>::replace(SetupState&& source) requires (!Const) {
    overflow = std::move(source.overflow); std::memcpy(&record, &source.record, sizeof(record));
}
struct SeededConfig {
    std::string_view build = "0.111.0+41cef1ea";
    std::string_view character = "CHARACTER.IRONCLAD";
    std::string_view seed = "ORACLECHECK01";
    std::uint8_t ascension = 0;
    std::array<std::string_view, 3> acts = {"ACT.OVERGROWTH", "ACT.HIVE", "ACT.GLORY"};
    ClockConfig clock;
};
// Builds persistent inventory, bags, all act encounter/event queues and RNG state
// from immutable definitions. Does not use a native save or expected snapshot.
[[nodiscard]] StepResult initialize_setup(SetupView, const SeededConfig&);
} // namespace sts2::engine
