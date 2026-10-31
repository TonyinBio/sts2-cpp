#pragma once
#include "sts2/content/catalog.hpp"
#include "sts2/state/clock.hpp"
#include "sts2/state/map.hpp"
#include "sts2/state/rng.hpp"
#include "sts2/state/cards.hpp"
#include "sts2/state/combat.hpp"
#include <algorithm>
#include <cstddef>
#include <iterator>
#include <optional>
#include <variant>
#include <stdexcept>

namespace sts2::engine {
struct RelicBagData {
    std::array<InlineSequence<ContentId, 64>, 8> tiers;
    InlineSequence<RelicTier, 8> order;
};
template<bool Const> struct BasicRelicBagView {
    using Data = std::conditional_t<Const, const RelicBagData, RelicBagData>;
    using Owner = std::conditional_t<Const, const OverflowStorage, OverflowStorage>;
    Data& data;
    Owner& overflow;
    std::array<SequenceView<ContentId, 64, Const>, 8> tiers;
    SequenceView<RelicTier, 8, Const> order;
    BasicRelicBagView(Data& r, Owner& o) noexcept : data(r), overflow(o),
        tiers{{{r.tiers[0], o}, {r.tiers[1], o}, {r.tiers[2], o}, {r.tiers[3], o}, {r.tiers[4], o}, {r.tiers[5], o}, {r.tiers[6], o}, {r.tiers[7], o}}}, order(r.order, o) {}
    template<bool Other> bool operator==(const BasicRelicBagView<Other>& other) const { return tiers == other.tiers && order == other.order; }
};
using RelicBag = BasicRelicBagView<false>;
using ConstRelicBag = BasicRelicBagView<true>;
struct ActRoomsData {
    ContentId act = no_content, ancient = no_content, boss = no_content, second_boss = no_content;
    InlineSequence<ContentId, 32> events, normal, elites, shared_ancients;
    std::uint32_t events_visited = 0, normal_visited = 0, elites_visited = 0, bosses_visited = 0;
};
template<bool Const> struct BasicActRoomsView {
    template<class T> using Ref = std::conditional_t<Const, const T&, T&>;
    using Data = std::conditional_t<Const, const ActRoomsData, ActRoomsData>;
    using Owner = std::conditional_t<Const, const OverflowStorage, OverflowStorage>;
    Data& data;
    Owner& overflow;
    Ref<ContentId> act = data.act;
    Ref<ContentId> ancient = data.ancient;
    Ref<ContentId> boss = data.boss;
    Ref<ContentId> second_boss = data.second_boss;
    SequenceView<ContentId, 32, Const> events{data.events, overflow};
    SequenceView<ContentId, 32, Const> normal{data.normal, overflow};
    SequenceView<ContentId, 32, Const> elites{data.elites, overflow};
    SequenceView<ContentId, 32, Const> shared_ancients{data.shared_ancients, overflow};
    Ref<std::uint32_t> events_visited = data.events_visited;
    Ref<std::uint32_t> normal_visited = data.normal_visited;
    Ref<std::uint32_t> elites_visited = data.elites_visited;
    Ref<std::uint32_t> bosses_visited = data.bosses_visited;
    BasicActRoomsView(Data& r, Owner& o) noexcept : data(r), overflow(o) {}
    template<bool Other> bool operator==(const BasicActRoomsView<Other>& other) const { return act == other.act && ancient == other.ancient && boss == other.boss && second_boss == other.second_boss && events == other.events && normal == other.normal && elites == other.elites && shared_ancients == other.shared_ancients && events_visited == other.events_visited && normal_visited == other.normal_visited && elites_visited == other.elites_visited && bosses_visited == other.bosses_visited; }
};
enum class RunPhase : std::uint8_t { uninitialized, neow, neow_done, map, combat, combat_done, rewards, room, complete, invalid };
enum class RewardReturn : std::uint8_t { map, parent_room };
enum class ParentRoomStage : std::uint8_t { child_combat, child_rewards, choosing };
struct ParentInteractionState {
    ParentRoomStage stage = ParentRoomStage::child_combat;
    std::int32_t pending_gold = 0;
    friend bool operator==(const ParentInteractionState&, const ParentInteractionState&) = default;
};
// Stateless rooms need no payload. An enclosing interaction can remain here
// while its child combat and generated offers live in their own records.
using RoomPayload = std::variant<std::monostate, ParentInteractionState>;
struct RewardOffers {
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
    RewardReturn return_to = RewardReturn::map;
    friend bool operator==(const RewardOffers&, const RewardOffers&) = default;
};
using CombatRewards = RewardOffers;
using CreaturePool = PoolData<CreatureState, CreatureTag, 8>;
// Views borrow pool storage. Reacquire them after any pool mutation.
template<bool Const> struct OptionalCombatView {
    using Data = std::conditional_t<Const, const std::optional<CombatData>, std::optional<CombatData>>;
    using Owner = std::conditional_t<Const, const OverflowStorage, OverflowStorage>;
    Data& data;
    Owner& overflow;
    PoolView<CreatureState, CreatureTag, 8, Const> creatures;
    std::conditional_t<Const, const CreatureHandle, CreatureHandle>& player;
    SequenceView<CreatureHandle, 4, Const> enemies;
    std::conditional_t<Const, const BoundaryId, BoundaryId>& boundary;
    std::conditional_t<Const, const RunClock, RunClock>& clock;
    std::conditional_t<Const, const RngBank, RngBank>& rng;
    std::conditional_t<Const, const std::uint32_t, std::uint32_t>& joss_paper_counter;
    bool has_value() const noexcept { return data.has_value(); }
    explicit operator bool() const noexcept { return has_value(); }
    auto operator*() const noexcept { return BasicCombatView<Const>(*data, overflow, creatures.owner, creatures.data, player, enemies.front(), boundary, clock, rng, joss_paper_counter); }
    auto operator->() const noexcept { return ViewArrow<BasicCombatView<Const>>{**this}; }
    auto emplace() requires (!Const) {
        reset();
        const auto enemy = creatures.create({}, UINT32_MAX);
        if (!enemy.present()) throw std::length_error("Creature identity exhausted");
        try { enemies.push_back(enemy); } catch (...) { (void)creatures.erase(enemy); throw; }
        overflow.reset(); data.emplace(); return **this;
    }
    void reset() noexcept requires (!Const) {
        // Combat execution and its captured operands are discarded before recycling.
        data.reset(); overflow.reset();
        for (auto enemy : enemies) {
            if (auto* creature = creatures.get(enemy)) creature->clear();
            (void)creatures.erase(enemy);
        }
        enemies.clear();
    }
    OptionalCombatView& operator=(CombatState&& state) requires (!Const) {
        if (!data) emplace();
        (**this).replace(std::move(state)); return *this;
    }
    OptionalCombatView& operator=(std::optional<CombatState>&& state) requires (!Const) {
        if (state) *this = std::move(*state); else reset(); return *this;
    }
    template<bool Other> bool operator==(const OptionalCombatView<Other>& other) const {
        return has_value() == other.has_value() && (!has_value() || **this == *other);
    }
};
using PersistentCardPool = PoolData<PersistentCard, PersistentCardTag, 32>;
template<bool Const> struct BasicDeckView {
    using Owner = std::conditional_t<Const, const OverflowStorage, OverflowStorage>;
    using Order = std::conditional_t<Const, const InlineSequence<PersistentCardHandle, 32>, InlineSequence<PersistentCardHandle, 32>>;
    PoolView<PersistentCard, PersistentCardTag, 32, Const> pool;
    Order& order;
    Owner& owner;
    SequenceView<PersistentCardExtra, 10, Const> extra;
    auto handles() const noexcept { return SequenceView<PersistentCardHandle, 32, Const>(order, owner); }
    std::size_t size() const noexcept { return handles().size(); }
    bool empty() const noexcept { return handles().empty(); }
    auto get(PersistentCardHandle h) const noexcept { return pool.get(h); }
    decltype(auto) operator[](std::size_t i) const noexcept { return *get(handles()[i]); }
    decltype(auto) front() const noexcept { return (*this)[0]; }
    decltype(auto) back() const noexcept { return (*this)[size() - 1]; }
    struct Iterator {
        BasicDeckView view;
        std::size_t index;
        using difference_type = std::ptrdiff_t;
        using value_type = PersistentCard;
        using iterator_category = std::forward_iterator_tag;
        decltype(auto) operator*() const { return view[index]; }
        Iterator& operator++() { ++index; return *this; }
        bool operator==(const Iterator& other) const { return index == other.index; }
    };
    Iterator begin() const noexcept { return {*this, 0}; }
    Iterator end() const noexcept { return {*this, size()}; }
    void reserve(std::size_t n) requires (!Const) { handles().reserve(n); pool.reserve(n); }
    void push_back(PersistentCard card) requires (!Const) {
        reserve(size() + 1);
        const auto handle = pool.create(card, UINT32_MAX);
        if (!handle.present()) throw std::length_error("persistent card pool exhausted");
        handles().push_back(handle);
    }
    bool erase(PersistentCardHandle handle) noexcept requires (!Const) {
        auto order_view = handles();
        const auto at = std::find(order_view.begin(), order_view.end(), handle);
        if (at == order_view.end() || !pool.erase(handle)) return false;
        order_view.erase(at);
        extra.erase(std::remove_if(extra.begin(), extra.end(), [&](const auto& entry) { return entry.card == handle; }), extra.end());
        return true;
    }
    void clear() noexcept requires (!Const) { while (!empty()) (void)erase(handles().back()); }
    template<bool Other> bool operator==(const BasicDeckView<Other>& other) const {
        return pool == other.pool && handles() == other.handles() && extra == other.extra;
    }
};
struct RunData {
    InlineSequence<char, 32> seed {};
    std::uint64_t numeric_seed = 0;
    ContentId character = no_content;
    std::uint8_t ascension = 0;
    CreaturePool creatures {};
    CreatureHandle player {};
    InlineSequence<CreatureHandle, 4> enemies {};
    std::int32_t gold = 0;
    std::int32_t potion_slots = 3;
    std::int32_t orb_slots = 0;
    RngBank rng {};
    PlayerRngBank player_rng {};
    RelicBagData shared_relics {};
    RelicBagData player_relics {};
    std::array<ActRoomsData, 3> acts {};
    PersistentCardPool persistent_cards {};
    InlineSequence<PersistentCardExtra, 10> card_extra {};
    InlineSequence<PersistentCardHandle, 32> deck {};
    InlineSequence<ContentId, 16> relics {};
    std::uint32_t joss_paper_counter = 0;
    InlineSequence<ContentId, 4> potions {};
    float card_rarity_offset = -0.05f;
    float potion_reward_odds = 0.4f;
    std::optional<CombatData> combat;
    bool direct_combat = false;
    std::uint32_t combat_generation = 0;
    std::optional<CombatRewards> rewards;
    RunClock clock;
    BoundaryId boundary{};
    RunPhase phase = RunPhase::uninitialized;
    RoomPayload room {};
    std::uint8_t act_index = 0;
    std::array<std::optional<MapState>, 3> maps;
    MapNodeId current = no_node;
    InlineSequence<MapNodeId, 64> visited;
    GameRng event_rng;
    std::array<ContentId, 3> neow_options{};
    ContentId selected_neow = no_content;
    template<class Visit> void for_each_sequence(const OverflowStorage& owner, Visit visit) const noexcept {
        for_each_sequence(visit);
        if (creatures.slots.valid(owner)) for (const auto& slot : creatures.slots.values(owner)) visit(slot.value.powers);
    }
    template<class Visit> void for_each_sequence(Visit visit) const noexcept {
        visit(seed); visit(creatures.slots); visit(creatures.free); visit(enemies);
        for (const auto* bag : {&shared_relics, &player_relics}) {
            for (const auto& tier : bag->tiers) visit(tier);
            visit(bag->order);
        }
        for (const auto& act : acts) { visit(act.events); visit(act.normal); visit(act.elites); visit(act.shared_ancients); }
        visit(persistent_cards.slots); visit(persistent_cards.free); visit(card_extra); visit(deck); visit(relics); visit(potions);
        visit(visited);
    }
};
static_assert(std::is_trivially_copyable_v<RunData>);
// Internal borrowed view: storage and handles must be validated before construction.
// Any mutation that can grow the creature pool invalidates existing views.
template<bool Const> struct BasicRunView {
    using Data = std::conditional_t<Const, const RunData, RunData>;
    using Owner = std::conditional_t<Const, const OverflowStorage, OverflowStorage>;
    template<class T> using Ref = std::conditional_t<Const, const T&, T&>;
    Data& data;
    Owner& overflow;
    TextView<32, Const> seed{data.seed, overflow};
    Ref<std::uint64_t> numeric_seed = data.numeric_seed;
    Ref<ContentId> character = data.character;
    Ref<std::uint8_t> ascension = data.ascension;
    PoolView<CreatureState, CreatureTag, 8, Const> creatures{data.creatures, overflow};
    Ref<std::int32_t> hp = creatures.get(data.player)->hp;
    Ref<std::int32_t> max_hp = creatures.get(data.player)->max_hp;
    Ref<std::int32_t> gold = data.gold;
    Ref<std::int32_t> potion_slots = data.potion_slots;
    Ref<std::int32_t> orb_slots = data.orb_slots;
    Ref<RngBank> rng = data.rng;
    Ref<PlayerRngBank> player_rng = data.player_rng;
    BasicRelicBagView<Const> shared_relics{data.shared_relics, overflow};
    BasicRelicBagView<Const> player_relics{data.player_relics, overflow};
    std::array<BasicActRoomsView<Const>, 3> acts{{{data.acts[0], overflow}, {data.acts[1], overflow}, {data.acts[2], overflow}}};
    SequenceView<PersistentCardExtra, 10, Const> card_extra{data.card_extra, overflow};
    BasicDeckView<Const> deck{{data.persistent_cards, overflow}, data.deck, overflow, card_extra};
    SequenceView<ContentId, 16, Const> relics{data.relics, overflow};
    Ref<std::uint32_t> joss_paper_counter = data.joss_paper_counter;
    SequenceView<ContentId, 4, Const> potions{data.potions, overflow};
    Ref<float> card_rarity_offset = data.card_rarity_offset;
    Ref<float> potion_reward_odds = data.potion_reward_odds;
    OptionalCombatView<Const> combat;
    Ref<bool> direct_combat = data.direct_combat;
    Ref<std::uint32_t> combat_generation = data.combat_generation;
    Ref<std::optional<CombatRewards>> rewards = data.rewards;
    Ref<RunClock> clock = data.clock;
    Ref<BoundaryId> boundary = data.boundary;
    Ref<RunPhase> phase = data.phase;
    Ref<RoomPayload> room = data.room;
    Ref<std::uint8_t> act_index = data.act_index;
    Ref<std::array<std::optional<MapState>, 3>> maps = data.maps;
    Ref<MapNodeId> current = data.current;
    SequenceView<MapNodeId, 64, Const> visited{data.visited, overflow};
    Ref<GameRng> event_rng = data.event_rng;
    Ref<std::array<ContentId, 3>> neow_options = data.neow_options;
    Ref<ContentId> selected_neow = data.selected_neow;
    BasicRunView(Data& r, Owner& run_owner, Owner& combat_owner) noexcept
        : data(r), overflow(run_owner), combat{r.combat, combat_owner, {r.creatures, run_owner}, r.player, {r.enemies, run_owner}, r.boundary, r.clock, r.rng, r.joss_paper_counter} {}
    BasicRunView view() const noexcept { return *this; }
    operator BasicRunView<true>() const noexcept requires (!Const) { return {data, overflow, combat.overflow}; }
    template<bool Other> bool operator==(const BasicRunView<Other>& other) const {
        return seed == other.seed && numeric_seed == other.numeric_seed &&
            character == other.character && ascension == other.ascension &&
            data.player == other.data.player && creatures == other.creatures &&
            combat.enemies == other.combat.enemies && gold == other.gold &&
            potion_slots == other.potion_slots && orb_slots == other.orb_slots &&
            rng == other.rng && player_rng == other.player_rng &&
            shared_relics == other.shared_relics && player_relics == other.player_relics &&
            acts == other.acts && deck == other.deck && card_extra == other.card_extra && relics == other.relics && joss_paper_counter == other.joss_paper_counter &&
            potions == other.potions && card_rarity_offset == other.card_rarity_offset &&
            potion_reward_odds == other.potion_reward_odds && combat == other.combat &&
            direct_combat == other.direct_combat && combat_generation == other.combat_generation &&
            rewards == other.rewards && clock == other.clock && boundary == other.boundary &&
            phase == other.phase && room == other.room && act_index == other.act_index && maps == other.maps &&
            current == other.current && visited == other.visited && event_rng == other.event_rng &&
            neow_options == other.neow_options && selected_neow == other.selected_neow;
    }
};
using RunView = BasicRunView<false>;
using ConstRunView = BasicRunView<true>;
inline bool valid_creature_storage(const RunData& data, const OverflowStorage& owner) noexcept {
    if (!valid_data_storage(data, owner)) return false;
    const PoolView<CreatureState, CreatureTag, 8, true> pool{data.creatures, owner};
    if (!pool.valid() || !pool.get(data.player)) return false;
    for (const auto& slot : pool.slots()) if (!slot.value.powers.valid(owner)) return false;
    const auto enemies = data.enemies.values(owner);
    if (data.combat.has_value() != !enemies.empty() || enemies.size() > 1) return false;
    for (const auto enemy : enemies) if (enemy == data.player || !pool.get(enemy)) return false;
    return true;
}
struct RunState;
CloneStatus clone_into(RunState&, const RunState&) noexcept;
struct RunState {
    RunData data;
    OverflowStorage overflow, combat_overflow;
    explicit RunState(StorageAllocator allocator = {}) : overflow(allocator), combat_overflow(allocator) {
        data.player = PoolView<CreatureState, CreatureTag, 8>{data.creatures, overflow}.create({}, UINT32_MAX);
    }
    RunState(const RunState& other) : RunState(other.overflow.allocator()) { if (clone_into(*this, other) != CloneStatus::cloned) throw std::bad_alloc{}; }
    RunState& operator=(const RunState& other) { if (clone_into(*this, other) != CloneStatus::cloned) throw std::bad_alloc{}; return *this; }
    RunState(RunState&& other) noexcept : overflow(std::move(other.overflow)), combat_overflow(std::move(other.combat_overflow)) {
        std::memcpy(&data, &other.data, sizeof(data));
    }
    RunState& operator=(RunState&& other) noexcept {
        if (this != &other) {
            overflow = std::move(other.overflow); combat_overflow = std::move(other.combat_overflow);
            std::memcpy(&data, &other.data, sizeof(data));
        }
        return *this;
    }
    void reset() noexcept {
        data = RunData{}; overflow.reset(); combat_overflow.reset();
        data.player = PoolView<CreatureState, CreatureTag, 8>{data.creatures, overflow}.create({}, UINT32_MAX);
    }
    RunView view() noexcept { return {data, overflow, combat_overflow}; }
    ConstRunView view() const noexcept { return {data, overflow, combat_overflow}; }
    friend CloneStatus clone_into(RunState& destination, const RunState& source) noexcept try {
        if (&destination == &source) return CloneStatus::cloned;
        // Prepare all ownership before publishing any part of the complete run.
        OverflowStorage run(destination.overflow.allocator()), combat(destination.combat_overflow.allocator());
        run.clone_from(source.overflow); combat.clone_from(source.combat_overflow);
        destination.overflow.swap(run); destination.combat_overflow.swap(combat);
        std::memcpy(&destination.data, &source.data, sizeof(RunData));
        return CloneStatus::cloned;
    } catch (const std::bad_alloc&) { return CloneStatus::resource_limit; }
      catch (const std::length_error&) { return CloneStatus::resource_limit; }
    friend bool operator==(const RunState& a, const RunState& b) { return a.view() == b.view(); }
};
} // namespace sts2::engine
