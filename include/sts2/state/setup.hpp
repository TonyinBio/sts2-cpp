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
struct PersistentCard {
    ContentId definition = no_content;
    std::uint32_t instance = 0;
    std::uint32_t upgrade = 0;
    std::int32_t floor_added = 1;
    friend bool operator==(const PersistentCard&, const PersistentCard&) = default;
};
struct SeededConfig {
    std::string_view build = "0.111.0+41cef1ea";
    std::string_view character = "CHARACTER.IRONCLAD";
    std::string_view seed = "ORACLECHECK01";
    std::uint8_t ascension = 0;
    std::array<std::string_view, 3> acts = {"ACT.OVERGROWTH", "ACT.HIVE", "ACT.GLORY"};
    ClockConfig clock;
};
} // namespace sts2::engine
