#include "sts2/persistence/checkpoint.hpp"
#include "sts2/kernel/simulator.hpp"
#include <bit>
#include <new>
#include <stdexcept>

namespace sts2::engine {
namespace {
struct Writer {
    std::vector<std::byte> data;
    void u8(std::uint8_t v) { data.push_back(std::byte(v)); }
    void u32(std::uint32_t v) { for (unsigned i = 0; i < 4; ++i) u8(static_cast<std::uint8_t>(v >> (i * 8))); }
    void u64(std::uint64_t v) { for (unsigned i = 0; i < 8; ++i) u8(static_cast<std::uint8_t>(v >> (i * 8))); }
    void i32(std::int32_t v) { u32(std::bit_cast<std::uint32_t>(v)); }
    void text(std::string_view s) { for (auto c : s) u8(static_cast<std::uint8_t>(c)); }
    void handle(CardHandle h) { u32(h.slot); u32(h.generation); }
    void creature(const CreatureState& c) { i32(c.hp); i32(c.max_hp); i32(c.block); i32(c.vulnerable); u8(c.shrink); i32(c.strength); i32(c.dexterity); i32(c.weak); }
};
struct Reader {
    std::span<const std::byte> bytes;
    std::size_t at = 0;
    std::uint8_t u8() { if (at == bytes.size()) throw std::invalid_argument("truncated checkpoint"); return std::to_integer<std::uint8_t>(bytes[at++]); }
    bool boolean() { const auto v = u8(); if (v > 1) throw std::invalid_argument("invalid boolean"); return v != 0; }
    std::uint32_t u32() { std::uint32_t v = 0; for (unsigned i = 0; i < 4; ++i) v |= std::uint32_t(u8()) << (8 * i); return v; }
    std::uint64_t u64() { std::uint64_t v = 0; for (unsigned i = 0; i < 8; ++i) v |= std::uint64_t(u8()) << (8 * i); return v; }
    std::int32_t i32() { return std::bit_cast<std::int32_t>(u32()); }
    CardHandle handle() { const auto slot = u32(); return {slot, u32()}; }
    CreatureState creature() { CreatureState c; c.hp = i32(); c.max_hp = i32(); c.block = i32(); c.vulnerable = i32(); c.shrink = boolean(); c.strength = i32(); c.dexterity = i32(); c.weak = i32(); return c; }
};
std::uint64_t digest(std::span<const std::byte> bytes) {
    return game_hash({reinterpret_cast<const char*>(bytes.data()), bytes.size()});
}
}
std::uint64_t content_fingerprint() {
    Writer w;
    for (const auto& card : Ruleset::pinned().cards()) {
        w.u32(static_cast<std::uint32_t>(card.model_id.size())); w.text(card.model_id);
        w.u32(static_cast<std::uint32_t>(card.name.size())); w.text(card.name);
        w.i32(card.cost); w.u8(card.enemy_target);
        w.u32(static_cast<std::uint32_t>(card.program.size()));
        for (const auto& instruction : card.program) { w.u8(static_cast<std::uint8_t>(instruction.opcode)); w.i32(instruction.base); w.i32(instruction.upgrade); }
    }
    // Version exceptional enemy/power/relic handlers and clock/RNG semantics too.
    w.text("shrinker-a0:niche:38-40:late-victory:retained-death:duration-history:shrink70:chomp7:stomp13;vulnerable150;burning-blood6;survivor1;action_clock_v1;xoshiro256ss-v1");
    return digest(w.data);
}
Checkpoint checkpoint(const State& s) {
    if (!valid_state(s)) throw std::invalid_argument("Cannot checkpoint invalid simulation");
    Writer w;
    w.text("STS2CP04"); w.u32(4); w.u32(s.rules); w.text(game_digest); w.u64(content_fingerprint());
    w.u32(static_cast<std::uint32_t>(s.seed.size())); w.text(s.seed); w.u64(s.numeric_seed);
    w.u32(s.boundary.value); w.u8(static_cast<std::uint8_t>(s.phase));
    const auto config = s.clock.config();
    w.u64(static_cast<std::uint64_t>(config.epoch_seconds)); w.u64(static_cast<std::uint64_t>(config.seconds_per_action));
    w.u64(static_cast<std::uint64_t>(s.clock.elapsed_seconds()));
    for (const auto& rng : s.rng.streams) { for (auto word : rng.state().words) w.u64(word); w.u32(rng.state().counter); }
    w.creature(s.player); w.creature(s.enemy); w.i32(s.energy); w.i32(s.turn);
    w.u8(static_cast<std::uint8_t>(s.move)); w.u8(s.burning_blood); w.u32(s.next_creation_order);
    w.u64(s.cards_played); w.u64(s.cards_drawn); w.u64(s.shuffles);
    w.u32(static_cast<std::uint32_t>(s.cards.slots().size()));
    for (const auto& slot : s.cards.slots()) {
        w.u32(slot.generation); w.u8(slot.live); w.u8(static_cast<std::uint8_t>(slot.value.definition));
        w.u8(slot.value.upgraded); w.u32(slot.value.creation_order); w.u32(slot.value.deck_instance); w.u8(slot.value.removed);
    }
    w.u32(static_cast<std::uint32_t>(s.cards.free_slots().size()));
    for (auto slot : s.cards.free_slots()) w.u32(slot);
    for (const auto& zone : s.zones) { w.u32(static_cast<std::uint32_t>(zone.size())); for (auto h : zone) w.handle(h); }
    w.u32(static_cast<std::uint32_t>(s.execution.size()));
    for (const auto& frame : s.execution) { w.handle(frame.source); w.u32(frame.instruction); w.u32(frame.play); }
    w.i32(s.damage_dealt); w.i32(s.damage_taken); w.i32(s.debuffs_applied); w.i32(s.healing);
    w.u32(static_cast<std::uint32_t>(s.plays.size()));
    for (const auto& p : s.plays) { w.handle(p.card); w.i32(p.energy); w.u8(p.enemy_target); }
    w.u32(static_cast<std::uint32_t>(s.powers.size()));
    for (const auto& p : s.powers) { w.u8(static_cast<std::uint8_t>(p.definition)); w.i32(p.amount); w.i32(p.amount_on_turn_start); w.u8(p.enemy_owner); w.u8(p.active); w.u8(p.skip_next_tick); w.u8(p.applier_enemy); w.u8(p.has_applier); w.handle(p.source); }
    w.u32(static_cast<std::uint32_t>(s.relics.size()));
    for (const auto& relic : s.relics) { w.u8(static_cast<std::uint8_t>(relic.definition)); w.i32(relic.counter); w.u8(relic.active); }
    w.u8(s.power_dispatch.active); w.u8(s.power_dispatch.enemy_side); w.u32(s.power_dispatch.cursor);
    w.u32(static_cast<std::uint32_t>(s.power_dispatch.candidates.size()));
    for (auto index : s.power_dispatch.candidates) w.u32(index);
    const auto& card_pass = s.card_played_dispatch;
    w.u32(card_pass.play); w.u32(card_pass.cursor); w.u32(card_pass.remove_power); w.u8(card_pass.stage); w.u8(card_pass.perturb); w.handle(card_pass.remove_card);
    const auto listeners = [&](const std::vector<ListenerRef>& list) {
        w.u32(static_cast<std::uint32_t>(list.size()));
        for (auto listener : list) { w.u8(static_cast<std::uint8_t>(listener.kind)); w.u32(listener.index); w.u32(listener.generation); }
    };
    listeners(card_pass.candidates); for (const auto& visits : card_pass.visited) listeners(visits);
    w.u32(static_cast<std::uint32_t>(s.history.size()));
    for (const auto& e : s.history) {
        w.u8(static_cast<std::uint8_t>(e.kind)); w.i32(e.turn); w.u8(e.enemy_side); w.u8(e.actor_enemy); w.handle(e.card); w.u32(e.object); w.i32(e.amount);
        w.i32(e.hit.blocked); w.i32(e.hit.unblocked); w.i32(e.hit.overkill); w.u8(e.hit.block_broken); w.u8(e.hit.fully_blocked); w.u8(e.hit.killed); w.u8(e.has_applier); w.u8(e.applier_enemy); w.i32(e.props);
    }
    w.u64(digest(w.data));
    return {std::move(w.data)};
}
RestoreStatus restore(State& destination, std::span<const std::byte> bytes, Scratch& scratch, Limits limits) noexcept {
    try {
        if (bytes.size() < 96 || bytes.size() > 64 * 1024 * 1024) return RestoreStatus::malformed;
        Reader trailer{bytes.last(8)};
        if (trailer.u64() != digest(bytes.first(bytes.size() - 8))) return RestoreStatus::malformed;
        Reader r{bytes.first(bytes.size() - 8)};
        for (char c : std::string_view("STS2CP04")) if (r.u8() != static_cast<std::uint8_t>(c)) return RestoreStatus::incompatible;
        if (r.u32() != 4 || r.u32() != rules_revision) return RestoreStatus::incompatible;
        for (char c : game_digest) if (r.u8() != static_cast<std::uint8_t>(c)) return RestoreStatus::incompatible;
        if (r.u64() != content_fingerprint()) return RestoreStatus::incompatible;
        State s;
        const auto seed_size = r.u32(); if (seed_size == 0 || seed_size > 4096) return RestoreStatus::malformed;
        for (std::uint32_t i = 0; i < seed_size; ++i) s.seed.push_back(static_cast<char>(r.u8()));
        s.numeric_seed = r.u64(); s.boundary.value = r.u32(); s.phase = static_cast<Phase>(r.u8());
        const auto epoch = r.u64(), step = r.u64(), elapsed = r.u64();
        if (epoch > RunClock::max_seconds || step > 86400 || elapsed > RunClock::max_seconds ||
            !s.clock.restore({static_cast<std::int64_t>(epoch), static_cast<std::int64_t>(step)}, static_cast<std::int64_t>(elapsed))) return RestoreStatus::malformed;
        for (auto& rng : s.rng.streams) {
            RngState state; for (auto& word : state.words) word = r.u64(); state.counter = r.u32();
            if (!rng.restore(state)) return RestoreStatus::malformed;
        }
        s.player = r.creature(); s.enemy = r.creature(); s.energy = r.i32(); s.turn = r.i32();
        s.move = static_cast<BeetleMove>(r.u8()); s.burning_blood = r.boolean(); s.next_creation_order = r.u32();
        s.cards_played = r.u64(); s.cards_drawn = r.u64(); s.shuffles = r.u64();
        const auto count = r.u32();
        if (count > limits.cards) return RestoreStatus::resource_limit;
        if (count > (r.bytes.size() - r.at) / 11) return RestoreStatus::malformed;
        std::vector<CardPool::Slot> slots(count);
        for (auto& slot : slots) {
            slot.generation = r.u32(); slot.live = r.boolean(); slot.value.definition = static_cast<CardId>(r.u8());
            slot.value.upgraded = r.boolean(); slot.value.creation_order = r.u32(); slot.value.deck_instance = r.u32(); slot.value.removed = r.boolean();
        }
        const auto free_count = r.u32(); if (free_count > count) return RestoreStatus::malformed;
        std::vector<std::uint32_t> free(free_count); for (auto& index : free) index = r.u32();
        if (!s.cards.restore(std::move(slots), std::move(free))) return RestoreStatus::malformed;
        s.reserve(count);
        std::uint64_t total = 0;
        for (auto& zone : s.zones) {
            const auto n = r.u32(); total += n;
            if (total > count) return RestoreStatus::malformed;
            for (std::uint32_t i = 0; i < n; ++i) zone.push_back(r.handle());
        }
        const auto frames = r.u32(); if (frames > 1) return RestoreStatus::malformed;
        for (std::uint32_t i = 0; i < frames; ++i) { auto source = r.handle(); const auto instruction = r.u32(); s.execution.push_back({source, instruction, r.u32()}); }
        s.damage_dealt = r.i32(); s.damage_taken = r.i32(); s.debuffs_applied = r.i32(); s.healing = r.i32();
        const auto bounded_count = [&](std::size_t width) {
            const auto n = r.u32();
            if (n > (r.bytes.size() - r.at) / width) throw std::invalid_argument("truncated record table");
            return n;
        };
        const auto plays = bounded_count(13); s.plays.reserve(plays);
        for (std::uint32_t i = 0; i < plays; ++i) { const auto card = r.handle(); const auto energy = r.i32(); s.plays.push_back({card, energy, r.boolean()}); }
        const auto powers = bounded_count(12); s.powers.reserve(powers);
        for (std::uint32_t i = 0; i < powers; ++i) {
            PowerRecord p; p.definition = static_cast<PowerId>(r.u8()); p.amount = r.i32(); p.amount_on_turn_start = r.i32();
            p.enemy_owner = r.boolean(); p.active = r.boolean(); p.skip_next_tick = r.boolean(); p.applier_enemy = r.boolean(); p.has_applier = r.boolean(); p.source = r.handle(); s.powers.push_back(p);
        }
        const auto relic_count = bounded_count(6);
        for (std::uint32_t i = 0; i < relic_count; ++i) { const auto definition = static_cast<RelicId>(r.u8()); const auto counter = r.i32(); s.relics.push_back({definition, counter, r.boolean()}); }
        s.power_dispatch.active = r.boolean(); s.power_dispatch.enemy_side = r.boolean(); s.power_dispatch.cursor = r.u32();
        const auto candidates = bounded_count(4);
        for (std::uint32_t i = 0; i < candidates; ++i) s.power_dispatch.candidates.push_back(r.u32());
        auto& card_pass = s.card_played_dispatch;
        card_pass.play = r.u32(); card_pass.cursor = r.u32(); card_pass.remove_power = r.u32(); card_pass.stage = r.u8(); card_pass.perturb = r.boolean(); card_pass.remove_card = r.handle();
        const auto listeners = [&](std::vector<ListenerRef>& list) {
            const auto size = bounded_count(9);
            for (std::uint32_t i = 0; i < size; ++i) {
                const auto kind = static_cast<ListenerKind>(r.u8()); const auto index = r.u32(); list.push_back({kind, index, r.u32()});
            }
        };
        listeners(card_pass.candidates); for (auto& visits : card_pass.visited) listeners(visits);
        const auto entries = bounded_count(44); s.history.reserve(entries);
        for (std::uint32_t i = 0; i < entries; ++i) {
            CombatHistoryEntry e; e.kind = static_cast<HistoryKind>(r.u8()); e.turn = r.i32(); e.enemy_side = r.boolean(); e.actor_enemy = r.boolean();
            e.card = r.handle(); e.object = r.u32(); e.amount = r.i32(); e.hit.blocked = r.i32(); e.hit.unblocked = r.i32(); e.hit.overkill = r.i32();
            e.hit.block_broken = r.boolean(); e.hit.fully_blocked = r.boolean(); e.hit.killed = r.boolean(); e.has_applier = r.boolean(); e.applier_enemy = r.boolean(); e.props = r.i32(); s.history.push_back(e);
        }
        if (r.at != r.bytes.size() || !valid_state(s)) return RestoreStatus::malformed;
        destination = std::move(s); scratch.steps = 0;
        return RestoreStatus::restored;
    } catch (const std::bad_alloc&) { return RestoreStatus::resource_limit; }
    catch (...) { return RestoreStatus::malformed; }
}
} // namespace sts2::engine
