#include "sts2/persistence/checkpoint.hpp"
#include "sts2/engine/combat.hpp"
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
    void creature(const CreatureState& c) {
        i32(c.hp); i32(c.max_hp); i32(c.block); u64(c.positive_unblocked_hits);
        u32(c.power_count);
        for (auto id : c.power_ids()) {
            const auto& p = c.power(id);
            u8(static_cast<std::uint8_t>(id)); u32(p.instance); i32(p.amount); i32(p.amount_on_turn_start);
            u8(p.enemy_owner); u8(p.skip_next_tick); u8(p.has_applier); u8(p.applier_enemy); handle(p.source);
        }
    }
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
    CreatureState creature() {
        CreatureState c; c.hp = i32(); c.max_hp = i32(); c.block = i32(); c.positive_unblocked_hits = u64();
        const auto count = u32();
        if (count > CreatureState::power_type_count) throw std::invalid_argument("Invalid power count");
        for (std::uint32_t i = 0; i < count; ++i) {
            const auto id = static_cast<PowerId>(u8());
            if (id >= PowerId::count || c.has_power(id)) throw std::invalid_argument("Invalid power slot");
            auto& p = c.power(id); p.definition = id; p.instance = u32(); p.amount = i32(); p.amount_on_turn_start = i32();
            p.enemy_owner = boolean(); p.skip_next_tick = boolean(); p.active = true;
            p.has_applier = boolean(); p.applier_enemy = boolean(); p.source = handle();
            c.power_order[c.power_count++] = id;
        }
        return c;
    }
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
        w.i32(card.cost); w.u8(card.enemy_target); w.u8(static_cast<std::uint8_t>(card.rarity));
        w.u32(static_cast<std::uint32_t>(card.program.size()));
        for (const auto& instruction : card.program) { w.u8(static_cast<std::uint8_t>(instruction.opcode)); w.i32(instruction.base); w.i32(instruction.upgrade); }
    }
    // Version exceptional enemy/power/relic handlers and clock/RNG semantics too.
    w.text("shrinker-a0:niche:38-40:late-victory:retained-death:duration-history:shrink70:chomp7:stomp13;vulnerable150;burning-blood6;survivor1;action_clock_v1;xoshiro256ss-v1");
    return digest(w.data);
}
Checkpoint checkpoint(ConstCombatView s) {
    if (!valid_combat(s)) throw std::invalid_argument("Cannot checkpoint invalid simulation");
    Writer w;
    w.text("STS2CP10"); w.u32(10); w.u32(s.rules); w.text(game_digest); w.u64(content_fingerprint());
    w.u32(static_cast<std::uint32_t>(s.seed.size())); w.text(s.seed); w.u64(s.numeric_seed);
    w.u32(s.boundary.value); w.u8(static_cast<std::uint8_t>(s.phase));
    const auto config = s.clock.config();
    w.u64(static_cast<std::uint64_t>(config.epoch_seconds)); w.u64(static_cast<std::uint64_t>(config.seconds_per_action));
    w.u64(static_cast<std::uint64_t>(s.clock.elapsed_seconds()));
    for (const auto& rng : s.rng.streams) { for (auto word : rng.state().words) w.u64(word); w.u32(rng.state().counter); }
    w.creature(s.player); w.creature(s.enemy); w.i32(s.energy); w.i32(s.turn);
    w.u8(static_cast<std::uint8_t>(s.move)); w.u8(s.burning_blood); w.u32(s.next_creation_order);
    w.u8(s.exhaust_relic_count); for (auto relic : s.exhaust_relics) w.u8(static_cast<std::uint8_t>(relic));
    w.u32(s.joss_paper_counter);
    w.handle(s.exhaust_dispatch.source); for (auto relic : s.exhaust_dispatch.candidates) w.u8(static_cast<std::uint8_t>(relic));
    w.u8(s.exhaust_dispatch.count); w.u8(s.exhaust_dispatch.cursor); w.u8(s.exhaust_dispatch.active);
    w.u32(s.cards.data.highest_generation);
    w.u32(static_cast<std::uint32_t>(s.cards.slots().size()));
    for (const auto& slot : s.cards.slots()) {
        w.u32(slot.generation); w.u8(slot.live); w.u8(static_cast<std::uint8_t>(slot.value.definition));
        w.u8(slot.value.upgraded); w.u32(slot.value.creation_order); w.u32(slot.value.origin.slot); w.u32(slot.value.origin.generation); w.i32(slot.value.last_finished_player_turn); w.u8(slot.value.removed);
    }
    w.u32(static_cast<std::uint32_t>(s.cards.free_slots().size()));
    for (auto slot : s.cards.free_slots()) w.u32(slot);
    for (const auto& zone : s.zone_views()) { w.u32(static_cast<std::uint32_t>(zone.size())); for (auto h : zone) w.handle(h); }
    w.u32(static_cast<std::uint32_t>(s.cost_modifiers.size()));
    for (const auto& modifier : s.cost_modifiers) { w.handle(modifier.card); w.u8(static_cast<std::uint8_t>(modifier.kind)); w.u8(static_cast<std::uint8_t>(modifier.expiry)); w.i32(modifier.amount); w.u8(modifier.reduce_only); }
    w.u32(static_cast<std::uint32_t>(s.execution.size()));
    for (const auto& frame : s.execution) { w.handle(frame.source); w.u32(frame.instruction); w.u32(frame.play); w.i32(frame.energy); w.u8(frame.enemy_target); }
    w.u32(s.next_prompt.value); w.u32(s.selection.id.value);
    w.u8(static_cast<std::uint8_t>(s.selection.effect)); w.u8(static_cast<std::uint8_t>(s.selection.zone));
    w.u32(s.selection.minimum); w.u32(s.selection.maximum); w.u8(s.selection.automatic_at_maximum);
    for (const auto& selection : {s.selection_candidates, s.selection_order}) {
        w.u32(static_cast<std::uint32_t>(selection.size())); for (auto card : selection) w.handle(card);
    }
    w.u32(s.counters.started_plays);
    w.u32(s.counters.finished_plays);
    w.u32(s.counters.started_this_turn);
    w.u32(s.counters.attacks_this_turn);
    w.u32(s.counters.zero_energy_attacks_this_turn);
    w.u32(s.counters.non_hand_draws_this_turn);
    w.u32(s.counters.finished_ethereal);
    w.u32(s.next_power_instance);
    w.u64(digest(w.data));
    return {std::move(w.data)};
}
RestoreStatus restore(CombatView destination, std::span<const std::byte> bytes, Scratch& scratch, Limits limits) noexcept {
    try {
        if (bytes.size() < 96 || bytes.size() > 64 * 1024 * 1024) return RestoreStatus::malformed;
        Reader trailer{bytes.last(8)};
        if (trailer.u64() != digest(bytes.first(bytes.size() - 8))) return RestoreStatus::malformed;
        Reader r{bytes.first(bytes.size() - 8)};
        for (char c : std::string_view("STS2CP10")) if (r.u8() != static_cast<std::uint8_t>(c)) return RestoreStatus::incompatible;
        if (r.u32() != 10 || r.u32() != rules_revision) return RestoreStatus::incompatible;
        for (char c : game_digest) if (r.u8() != static_cast<std::uint8_t>(c)) return RestoreStatus::incompatible;
        if (r.u64() != content_fingerprint()) return RestoreStatus::incompatible;
        CombatState s(destination.overflow.allocator()); auto s_view = s.view();
        const auto seed_size = r.u32(); if (seed_size == 0 || seed_size > 4096) return RestoreStatus::malformed;
        for (std::uint32_t i = 0; i < seed_size; ++i) s_view.seed.push_back(static_cast<char>(r.u8()));
        s_view.numeric_seed = r.u64(); s_view.boundary.value = r.u32(); s_view.phase = static_cast<CombatPhase>(r.u8());
        const auto epoch = r.u64(), step = r.u64(), elapsed = r.u64();
        if (epoch > RunClock::max_seconds || step > 86400 || elapsed > RunClock::max_seconds ||
            !s_view.clock.restore({static_cast<std::int64_t>(epoch), static_cast<std::int64_t>(step)}, static_cast<std::int64_t>(elapsed))) return RestoreStatus::malformed;
        for (auto& rng : s_view.rng.streams) {
            RngState state; for (auto& word : state.words) word = r.u64(); state.counter = r.u32();
            if (!rng.restore(state)) return RestoreStatus::malformed;
        }
        s_view.player = r.creature(); s_view.enemy = r.creature(); s_view.energy = r.i32(); s_view.turn = r.i32();
        s_view.move = static_cast<BeetleMove>(r.u8()); s_view.burning_blood = r.boolean(); s_view.next_creation_order = r.u32();
        s_view.exhaust_relic_count = r.u8(); for (auto& relic : s_view.exhaust_relics) relic = static_cast<ExhaustRelic>(r.u8());
        s_view.joss_paper_counter = r.u32();
        s_view.exhaust_dispatch.source = r.handle(); for (auto& relic : s_view.exhaust_dispatch.candidates) relic = static_cast<ExhaustRelic>(r.u8());
        s_view.exhaust_dispatch.count = r.u8(); s_view.exhaust_dispatch.cursor = r.u8(); s_view.exhaust_dispatch.active = r.boolean();
        const auto highest_generation = r.u32();
        const auto count = r.u32();
        if (count > limits.cards) return RestoreStatus::resource_limit;
        if (count > (r.bytes.size() - r.at) / 11) return RestoreStatus::malformed;
        std::vector<CardPool::Slot> slots(count);
        for (auto& slot : slots) {
            slot.generation = r.u32(); slot.live = r.boolean(); slot.value.definition = static_cast<CardId>(r.u8());
            slot.value.upgraded = r.boolean(); slot.value.creation_order = r.u32(); slot.value.origin.slot = r.u32(); slot.value.origin.generation = r.u32(); slot.value.last_finished_player_turn = r.i32(); slot.value.removed = r.boolean();
        }
        const auto free_count = r.u32(); if (free_count > count) return RestoreStatus::malformed;
        std::vector<std::uint32_t> free(free_count); for (auto& index : free) index = r.u32();
        if (!s_view.cards.restore(std::move(slots), std::move(free))) return RestoreStatus::malformed;
        if (s_view.cards.data.highest_generation > highest_generation) return RestoreStatus::malformed;
        s_view.cards.data.highest_generation = highest_generation;
        s_view.reserve(count);
        std::uint64_t total = 0;
        for (auto& zone : s_view.zone_views()) {
            const auto n = r.u32(); total += n;
            if (total > count) return RestoreStatus::malformed;
            for (std::uint32_t i = 0; i < n; ++i) zone.push_back(r.handle());
        }
        const auto modifiers = r.u32(); if (modifiers > (r.bytes.size() - r.at) / 15) return RestoreStatus::malformed;
        for (std::uint32_t i = 0; i < modifiers; ++i) { CostModifier modifier; modifier.card = r.handle(); modifier.kind = static_cast<CostModifierKind>(r.u8()); modifier.expiry = static_cast<CostExpiry>(r.u8()); modifier.amount = r.i32(); modifier.reduce_only = r.boolean(); s_view.cost_modifiers.push_back(modifier); }
        const auto frames = r.u32(); if (frames > 1) return RestoreStatus::malformed;
        for (std::uint32_t i = 0; i < frames; ++i) {
            EffectFrame frame; frame.source = r.handle(); frame.instruction = r.u32(); frame.play = r.u32();
            frame.energy = r.i32(); frame.enemy_target = r.boolean(); s_view.execution.push_back(frame);
        }
        s_view.next_prompt.value = r.u32(); s_view.selection.id.value = r.u32();
        s_view.selection.effect = static_cast<SelectionEffect>(r.u8()); s_view.selection.zone = static_cast<Zone>(r.u8());
        s_view.selection.minimum = r.u32(); s_view.selection.maximum = r.u32(); s_view.selection.automatic_at_maximum = r.boolean();
        for (auto selection : {s_view.selection_candidates, s_view.selection_order}) {
            const auto n = r.u32(); if (n > count) return RestoreStatus::malformed;
            for (std::uint32_t i = 0; i < n; ++i) selection.push_back(r.handle());
        }
        s_view.counters.started_plays = r.u32();
        s_view.counters.finished_plays = r.u32();
        s_view.counters.started_this_turn = r.u32();
        s_view.counters.attacks_this_turn = r.u32();
        s_view.counters.zero_energy_attacks_this_turn = r.u32();
        s_view.counters.non_hand_draws_this_turn = r.u32();
        s_view.counters.finished_ethereal = r.u32();
        s_view.next_power_instance = r.u32();
        if (r.at != r.bytes.size() || !valid_combat(s)) return RestoreStatus::malformed;
        destination.replace(std::move(s)); scratch.steps = 0;
        return RestoreStatus::restored;
    } catch (const std::bad_alloc&) { return RestoreStatus::resource_limit; } catch (const std::length_error&) { return RestoreStatus::resource_limit; }
    catch (...) { return RestoreStatus::malformed; }
}
} // namespace sts2::engine
