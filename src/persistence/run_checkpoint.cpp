#include "sts2/persistence/checkpoint.hpp"
#include "sts2/kernel/run_simulator.hpp"
#include <bit>
#include <new>
#include <stdexcept>

namespace sts2::engine {
namespace {
struct Capacity {};
struct Writer {
    std::vector<std::byte> bytes;
    void u8(std::uint8_t value) { bytes.push_back(std::byte(value)); }
    void u32(std::uint32_t value) { for (unsigned i = 0; i < 4; ++i) u8(static_cast<std::uint8_t>(value >> (8 * i))); }
    void u64(std::uint64_t value) { for (unsigned i = 0; i < 8; ++i) u8(static_cast<std::uint8_t>(value >> (8 * i))); }
    void i32(std::int32_t value) { u32(std::bit_cast<std::uint32_t>(value)); }
    void text(std::string_view value) { for (auto c : value) u8(static_cast<std::uint8_t>(c)); }
    void rng(const RngState& value) { for (auto word : value.words) u64(word); u32(value.counter); }
    void ids(std::span<const ContentId> values) { u32(static_cast<std::uint32_t>(values.size())); for (auto value : values) u32(value); }
    void card(const PersistentCard& value) { u32(value.definition); u32(value.instance); u32(value.upgrade); i32(value.floor_added); }
    void edges(const MapEdges& value) {
        for (auto node : value.slots) u32(node);
        for (auto slot : value.free) u8(slot);
        u8(value.used); u8(value.free_count);
    }
    void bag(ConstRelicBag value) {
        for (const auto& tier : value.tiers) ids(tier);
        u32(static_cast<std::uint32_t>(value.order.size())); for (auto tier : value.order) u8(static_cast<std::uint8_t>(tier));
    }
    void map(const MapState& value) {
        u32(value.count); i32(value.height); u32(value.start); u32(value.boss); u32(value.second_boss); edges(value.starts); rng(value.generator_state);
        for (auto node : value.grid) u32(node);
        for (const auto& node : value.nodes) {
            i32(node.col); i32(node.row); u8(static_cast<std::uint8_t>(node.kind)); u8(node.can_modify); u8(node.active); edges(node.children); edges(node.parents);
        }
    }
};
struct Reader {
    std::span<const std::byte> bytes;
    std::size_t at = 0;
    std::uint8_t u8() { if (at == bytes.size()) throw std::invalid_argument("Truncated run checkpoint"); return std::to_integer<std::uint8_t>(bytes[at++]); }
    bool boolean() { const auto value = u8(); if (value > 1) throw std::invalid_argument("Invalid boolean"); return value != 0; }
    std::uint32_t u32() { std::uint32_t value = 0; for (unsigned i = 0; i < 4; ++i) value |= std::uint32_t(u8()) << (8 * i); return value; }
    std::uint64_t u64() { std::uint64_t value = 0; for (unsigned i = 0; i < 8; ++i) value |= std::uint64_t(u8()) << (8 * i); return value; }
    std::int32_t i32() { return std::bit_cast<std::int32_t>(u32()); }
    std::uint16_t id() { const auto value = u32(); if (value > UINT16_MAX) throw std::invalid_argument("Invalid compact ID"); return static_cast<std::uint16_t>(value); }
    std::size_t count(std::size_t width, std::size_t maximum = 65536) {
        const auto n = u32(); if (n > maximum) throw Capacity{};
        if (n > (bytes.size() - at) / width) throw std::invalid_argument("Truncated run table");
        return n;
    }
    RngState rng() { RngState result; for (auto& word : result.words) word = u64(); result.counter = u32(); return result; }
    void random(GameRng& value) { if (!value.restore(rng())) throw std::invalid_argument("Zero random state"); }
    template<std::size_t N> void ids(SequenceView<ContentId, N> values) { const auto n = count(4); values.reserve(n); for (std::size_t i = 0; i < n; ++i) values.push_back(id()); }
    PersistentCard card() { PersistentCard result; result.definition = id(); result.instance = u32(); result.upgrade = u32(); result.floor_added = i32(); return result; }
    void edges(MapEdges& value) {
        for (auto& node : value.slots) node = id();
        for (auto& slot : value.free) slot = u8();
        value.used = u8(); value.free_count = u8();
    }
    void bag(RelicBag value) {
        for (auto& tier : value.tiers) ids(tier);
        const auto n = count(1, 8); value.order.reserve(n);
        for (std::size_t i = 0; i < n; ++i) value.order.push_back(static_cast<RelicTier>(u8()));
    }
    void map(MapState& value) {
        value.count = id(); value.height = i32(); value.start = id(); value.boss = id(); value.second_boss = id(); edges(value.starts); value.generator_state = rng();
        for (auto& node : value.grid) node = id();
        for (auto& node : value.nodes) {
            node.col = i32(); node.row = i32(); node.kind = static_cast<MapKind>(u8()); node.can_modify = boolean(); node.active = boolean(); edges(node.children); edges(node.parents);
        }
    }
};
std::uint64_t checksum(std::span<const std::byte> bytes) { return game_hash({reinterpret_cast<const char*>(bytes.data()), bytes.size()}); }
}
Checkpoint checkpoint(const RunState& run) {
    auto run_view = run.view();
    if (!valid_run(run)) throw std::invalid_argument("Cannot checkpoint invalid run");
    Writer w; w.text("STS2RUN1"); w.u32(3); w.u32(rules_revision); w.text(game_digest); w.text(SetupRules::fingerprint()); w.u64(content_fingerprint());
    const auto s = run_view.setup;
    w.u32(static_cast<std::uint32_t>(s.seed.size())); w.text(s.seed); w.u64(s.numeric_seed); w.u32(s.character); w.u8(s.ascension);
    w.i32(s.hp); w.i32(s.max_hp); w.i32(s.gold); w.i32(s.potion_slots); w.i32(s.orb_slots);
    for (const auto& rng : s.rng.streams) w.rng(rng.state());
    for (const auto& rng : s.player_rng.streams) w.rng(rng.state());
    w.bag(s.shared_relics); w.bag(s.player_relics);
    for (const auto& act : s.acts) {
        w.u32(act.act); w.u32(act.ancient); w.u32(act.boss); w.u32(act.second_boss);
        w.ids(act.events); w.ids(act.normal); w.ids(act.elites); w.ids(act.shared_ancients);
        w.u32(act.events_visited); w.u32(act.normal_visited); w.u32(act.elites_visited); w.u32(act.bosses_visited);
    }
    w.u32(static_cast<std::uint32_t>(s.deck.size())); for (const auto& card : s.deck) w.card(card);
    w.ids(s.relics); w.ids(s.potions); w.u32(std::bit_cast<std::uint32_t>(s.card_rarity_offset)); w.u32(std::bit_cast<std::uint32_t>(s.potion_reward_odds));
    const auto clock = run_view.clock.config(); w.u64(static_cast<std::uint64_t>(clock.epoch_seconds)); w.u64(static_cast<std::uint64_t>(clock.seconds_per_action)); w.u64(static_cast<std::uint64_t>(run_view.clock.elapsed_seconds()));
    w.u32(run_view.boundary.value); w.u8(static_cast<std::uint8_t>(run_view.phase)); w.u8(run_view.act_index);
    for (const auto& map : run_view.maps) { w.u8(map.has_value()); if (map) w.map(*map); }
    w.u32(run_view.current); w.ids(run_view.visited); w.rng(run_view.event_rng.state());
    for (auto option : run_view.neow_options) w.u32(option);
    w.u32(run_view.selected_neow); w.u8(run_view.direct_combat); w.u32(run_view.combat_generation);
    w.u8(run_view.combat.has_value());
    if (run_view.combat) { const auto combat = checkpoint(*run_view.combat); w.u32(static_cast<std::uint32_t>(combat.bytes.size())); w.bytes.insert(w.bytes.end(), combat.bytes.begin(), combat.bytes.end()); }
    w.u8(run_view.rewards.has_value());
    if (run_view.rewards) {
        const auto& rewards = *run_view.rewards; w.i32(rewards.gold); w.u32(rewards.potion); for (const auto& card : rewards.cards) w.card(card);
        w.u8(rewards.gold_taken); w.u8(rewards.potion_taken); w.u8(rewards.card_taken); w.u8(rewards.selecting_card); w.u8(rewards.resolved); w.u8(rewards.skipped); w.u8(rewards.selected_card);
    }
    w.u64(checksum(w.bytes)); return {std::move(w.bytes)};
}
RestoreStatus restore(RunState& destination, std::span<const std::byte> bytes, Scratch& scratch, Limits limits) noexcept {
    try {
        if (bytes.size() < 160 || bytes.size() > 64 * 1024 * 1024) return RestoreStatus::malformed;
        Reader trailer{bytes.last(8)}; if (trailer.u64() != checksum(bytes.first(bytes.size() - 8))) return RestoreStatus::malformed;
        Reader r{bytes.first(bytes.size() - 8)};
        for (char c : std::string_view("STS2RUN1")) if (r.u8() != static_cast<std::uint8_t>(c)) return RestoreStatus::incompatible;
        if (r.u32() != 3 || r.u32() != rules_revision) return RestoreStatus::incompatible;
        for (const auto identity : {game_digest, SetupRules::fingerprint()}) for (char c : identity)
            if (r.u8() != static_cast<std::uint8_t>(c)) return RestoreStatus::incompatible;
        if (r.u64() != content_fingerprint()) return RestoreStatus::incompatible;
        RunState run(destination.overflow.allocator()); auto run_view = run.view(); auto s = run_view.setup;
        const auto seed_size = r.count(1, 4096); for (std::size_t i = 0; i < seed_size; ++i) s.seed.push_back(static_cast<char>(r.u8()));
        s.numeric_seed = r.u64(); s.character = r.id(); s.ascension = r.u8();
        s.hp = r.i32(); s.max_hp = r.i32(); s.gold = r.i32(); s.potion_slots = r.i32(); s.orb_slots = r.i32();
        for (auto& rng : s.rng.streams) r.random(rng);
        for (auto& rng : s.player_rng.streams) r.random(rng);
        r.bag(s.shared_relics); r.bag(s.player_relics);
        for (auto& act : s.acts) {
            act.act = r.id(); act.ancient = r.id(); act.boss = r.id(); act.second_boss = r.id();
            r.ids(act.events); r.ids(act.normal); r.ids(act.elites); r.ids(act.shared_ancients);
            act.events_visited = r.u32(); act.normal_visited = r.u32(); act.elites_visited = r.u32(); act.bosses_visited = r.u32();
        }
        const auto cards = r.count(16, limits.cards); s.deck.reserve(cards); for (std::size_t i = 0; i < cards; ++i) s.deck.push_back(r.card());
        r.ids(s.relics); r.ids(s.potions); s.card_rarity_offset = std::bit_cast<float>(r.u32()); s.potion_reward_odds = std::bit_cast<float>(r.u32());
        const auto epoch = r.u64(), step = r.u64(), elapsed = r.u64();
        if (epoch > RunClock::max_seconds || step > 86400 || elapsed > RunClock::max_seconds ||
            !run_view.clock.restore({static_cast<std::int64_t>(epoch), static_cast<std::int64_t>(step)}, static_cast<std::int64_t>(elapsed))) return RestoreStatus::malformed;
        run_view.boundary.value = r.u32(); run_view.phase = static_cast<RunPhase>(r.u8()); run_view.act_index = r.u8();
        for (auto& map : run_view.maps) if (r.boolean()) { map.emplace(); r.map(*map); }
        run_view.current = r.id(); r.ids(run_view.visited); r.random(run_view.event_rng);
        for (auto& option : run_view.neow_options) option = r.id();
        run_view.selected_neow = r.id(); run_view.direct_combat = r.boolean(); run_view.combat_generation = r.u32();
        Scratch temporary;
        if (r.boolean()) {
            const auto size = r.count(1, 64 * 1024 * 1024); run_view.combat.emplace();
            const auto status = restore(*run_view.combat, r.bytes.subspan(r.at, size), temporary, limits); if (status != RestoreStatus::restored) return status; r.at += size;
        }
        if (r.boolean()) {
            run_view.rewards.emplace(); auto& rewards = *run_view.rewards; rewards.gold = r.i32(); rewards.potion = r.id();
            for (auto& card : rewards.cards) card = r.card();
            rewards.gold_taken = r.boolean(); rewards.potion_taken = r.boolean(); rewards.card_taken = r.boolean(); rewards.selecting_card = r.boolean();
            rewards.resolved = r.boolean(); rewards.skipped = r.boolean(); rewards.selected_card = r.u8();
        }
        if (r.at != r.bytes.size() || !valid_run(run)) return RestoreStatus::malformed;
        destination = std::move(run); scratch.steps = 0; return RestoreStatus::restored;
    } catch (const Capacity&) { return RestoreStatus::resource_limit; }
    catch (const std::bad_alloc&) { return RestoreStatus::resource_limit; } catch (const std::length_error&) { return RestoreStatus::resource_limit; }
    catch (...) { return RestoreStatus::malformed; }
}
} // namespace sts2::engine
