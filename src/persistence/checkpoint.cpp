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
    void reaction_handle(ShuffleReactionHandle h) { u32(h.slot); u32(h.generation); }
    void frame(const EffectFrame& f) {
        handle(f.source); u32(f.instruction); u32(f.play); i32(f.energy); u32(f.target.slot); u32(f.target.generation);
        u8(static_cast<std::uint8_t>(f.pending.index()));
        if (const auto* value = std::get_if<DrawContinuation>(&f.pending)) { i32(value->remaining); u8(value->from_hand_draw); }
        else if (const auto* value = std::get_if<ShuffleContinuation>(&f.pending)) { u32(value->begin); u32(value->count); u32(value->cursor); u8(value->waiting); }
        else if (const auto* value = std::get_if<SelectionContinuation>(&f.pending)) { u8(static_cast<std::uint8_t>(value->effect)); u8(static_cast<std::uint8_t>(value->zone)); u32(value->begin); u32(value->count); u32(value->cursor); }
        else if (const auto* value = std::get_if<ExhaustContinuation>(&f.pending)) { for (auto relic : value->candidates) u8(static_cast<std::uint8_t>(relic)); u8(value->count); u8(value->cursor); }
    }
    void card_extra(const CardExtraData& data) {
        u8(static_cast<std::uint8_t>(data.index()));
        const auto& scythe = std::get<TheScytheExtraData>(data);
        i32(scythe.current_damage); i32(scythe.increased_damage);
    }
    void creature(const CreatureState& c, const OverflowStorage& owner) {
        i32(c.hp); i32(c.max_hp); i32(c.block); u64(c.positive_unblocked_hits);
        u32(static_cast<std::uint32_t>(c.powers.size()));
        for (const auto& p : c.power_instances(owner)) {
            const auto id = p.definition;
            u8(static_cast<std::uint8_t>(id)); u32(p.instance); i32(p.amount); i32(p.amount_on_turn_start);
            u8(p.enemy_owner); u8(p.skip_next_tick); u8(p.has_applier); u8(p.applier_enemy); handle(p.source); u32(p.applier.slot); u32(p.applier.generation); u8(static_cast<std::uint8_t>(p.stacking));
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
    CardExtraData card_extra() {
        switch (u8()) {
        case 0: return TheScytheExtraData{i32(), i32()};
        default: throw std::invalid_argument("Unknown card extra data");
        }
    }
    CardHandle handle() { const auto slot = u32(); return {slot, u32()}; }
    std::uint32_t count(std::size_t width, std::uint64_t limit) {
        const auto n = u32(); if (n > limit) throw std::length_error("Continuation capacity exceeded");
        if (n > (bytes.size() - at) / width) throw std::invalid_argument("Truncated continuation data");
        return n;
    }
    ShuffleReactionHandle reaction_handle() { const auto slot = u32(); return {slot, u32()}; }
    EffectFrame frame() {
        EffectFrame f; f.source = handle(); f.instruction = u32(); f.play = u32(); f.energy = i32(); f.target = {u32(), u32()};
        switch (u8()) {
        case 0: break;
        case 1: f.pending = DrawContinuation{i32(), boolean()}; break;
        case 2: f.pending = ShuffleContinuation{u32(), u32(), u32(), boolean()}; break;
        case 3: f.pending = SelectionContinuation{static_cast<SelectionEffect>(u8()), static_cast<Zone>(u8()), u32(), u32(), u32()}; break;
        case 5: f.pending = RelicDrawCompletion{}; break;
        case 4: { ExhaustContinuation value; for (auto& relic : value.candidates) relic = static_cast<ExhaustRelic>(u8()); value.count = u8(); value.cursor = u8(); f.pending = value; break; }
        default: throw std::invalid_argument("Unknown continuation");
        }
        return f;
    }
    CreatureState creature(OverflowStorage& owner) {
        CreatureState c; c.hp = i32(); c.max_hp = i32(); c.block = i32(); c.positive_unblocked_hits = u64();
        const auto count = u32();
        if (count > 65536) throw std::invalid_argument("Invalid power count");
        for (std::uint32_t i = 0; i < count; ++i) {
            const auto id = static_cast<PowerId>(u8());
            if (id >= PowerId::count) throw std::invalid_argument("Invalid power slot");
            PowerInstance p; p.definition = id; p.instance = u32(); p.amount = i32(); p.amount_on_turn_start = i32();
            p.enemy_owner = boolean(); p.skip_next_tick = boolean(); p.active = true;
            p.has_applier = boolean(); p.applier_enemy = boolean(); p.source = handle(); p.applier = {u32(), u32()}; p.stacking = static_cast<PowerInstance::Stacking>(u8());
            c.power_instances(owner).push_back(p);
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
        w.u8(static_cast<std::uint8_t>(card.kind)); w.u8(static_cast<std::uint8_t>(card.destination)); w.u8(card.retain);
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
    w.text("STS2CP15"); w.u32(15); w.u32(s.rules); w.text(game_digest); w.u64(content_fingerprint());
    w.u32(static_cast<std::uint32_t>(s.seed.size())); w.text(s.seed); w.u64(s.numeric_seed);
    w.u32(s.boundary.value); w.u8(static_cast<std::uint8_t>(s.phase));
    const auto config = s.clock.config();
    w.u64(static_cast<std::uint64_t>(config.epoch_seconds)); w.u64(static_cast<std::uint64_t>(config.seconds_per_action));
    w.u64(static_cast<std::uint64_t>(s.clock.elapsed_seconds()));
    for (const auto& rng : s.rng.streams) { for (auto word : rng.state().words) w.u64(word); w.u32(rng.state().counter); }
    w.u32(s.player_handle.slot); w.u32(s.player_handle.generation); w.u32(s.enemy_handle.slot); w.u32(s.enemy_handle.generation);
    w.creature(s.player, s.creature_overflow); w.creature(s.enemy, s.creature_overflow); w.i32(s.energy); w.i32(s.turn);
    w.u8(static_cast<std::uint8_t>(s.move)); w.u8(s.burning_blood); w.u32(s.next_creation_order);
    w.u8(s.exhaust_relic_count); for (auto relic : s.exhaust_relics) w.u8(static_cast<std::uint8_t>(relic));
    w.u32(s.joss_paper_counter);
    w.u32(s.cards.data.highest_generation);
    w.u32(static_cast<std::uint32_t>(s.cards.slots().size()));
    for (const auto& slot : s.cards.slots()) {
        w.u32(slot.generation); w.u8(slot.live); w.u8(static_cast<std::uint8_t>(slot.value.definition));
        w.u8(slot.value.upgraded); w.u32(slot.value.creation_order); w.u32(slot.value.origin.slot); w.u32(slot.value.origin.generation); w.i32(slot.value.last_finished_player_turn); w.u8(slot.value.removed);
    }
    w.u32(static_cast<std::uint32_t>(s.card_extra.size()));
    for (const auto& entry : s.card_extra) { w.handle(entry.card); w.card_extra(entry.data); }
    w.u32(static_cast<std::uint32_t>(s.cards.free_slots().size()));
    for (auto slot : s.cards.free_slots()) w.u32(slot);
    for (const auto& zone : s.zone_views()) { w.u32(static_cast<std::uint32_t>(zone.size())); for (auto h : zone) w.handle(h); }
    w.u32(static_cast<std::uint32_t>(s.cost_modifiers.size()));
    for (const auto& modifier : s.cost_modifiers) { w.handle(modifier.card); w.u8(static_cast<std::uint8_t>(modifier.kind)); w.u8(static_cast<std::uint8_t>(modifier.expiry)); w.i32(modifier.amount); w.u8(modifier.reduce_only); }
    w.u32(s.shuffle_reactions.data.highest_generation);
    w.u32(static_cast<std::uint32_t>(s.shuffle_reactions.slots().size()));
    for (const auto& slot : s.shuffle_reactions.slots()) { w.u32(slot.generation); w.u8(slot.live); w.u8(static_cast<std::uint8_t>(slot.value.kind)); w.u8(slot.value.fired); }
    w.u32(static_cast<std::uint32_t>(s.shuffle_reactions.free_slots().size()));
    for (auto index : s.shuffle_reactions.free_slots()) w.u32(index);
    w.u32(static_cast<std::uint32_t>(s.shuffle_order.size())); for (auto h : s.shuffle_order) w.reaction_handle(h);
    w.u32(static_cast<std::uint32_t>(s.captured_shuffle_reactions.size())); for (auto h : s.captured_shuffle_reactions) w.reaction_handle(h);
    w.u32(static_cast<std::uint32_t>(s.continuation_cards.size())); for (auto h : s.continuation_cards) w.handle(h);
    w.u32(static_cast<std::uint32_t>(s.execution.size())); for (const auto& frame : s.execution) w.frame(frame);
    w.u32(s.next_prompt.value); w.u32(s.selection.id.value);
    w.u8(static_cast<std::uint8_t>(s.selection.effect)); w.u8(static_cast<std::uint8_t>(s.selection.zone));
    w.u32(s.selection.minimum); w.u32(s.selection.maximum); w.u8(s.selection.automatic_at_maximum); w.u32(s.selection.continuation_depth);
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
        for (char c : std::string_view("STS2CP15")) if (r.u8() != static_cast<std::uint8_t>(c)) return RestoreStatus::incompatible;
        if (r.u32() != 15 || r.u32() != rules_revision) return RestoreStatus::incompatible;
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
        s.data.player_identity = {r.u32(), r.u32()}; s.data.enemy_identity = {r.u32(), r.u32()}; s_view.player_handle = s.data.player_identity; s_view.enemy_handle = s.data.enemy_identity;
        if (destination.creature_pool && (s_view.player_handle != destination.player_handle || s_view.enemy_handle != destination.enemy_handle)) return RestoreStatus::malformed;
        s_view.player = r.creature(s.creature_overflow); s_view.enemy = r.creature(s.creature_overflow); s_view.energy = r.i32(); s_view.turn = r.i32();
        s_view.move = static_cast<BeetleMove>(r.u8()); s_view.burning_blood = r.boolean(); s_view.next_creation_order = r.u32();
        s_view.exhaust_relic_count = r.u8(); for (auto& relic : s_view.exhaust_relics) relic = static_cast<ExhaustRelic>(r.u8());
        s_view.joss_paper_counter = r.u32();
        const auto highest_generation = r.u32();
        const auto count = r.u32();
        if (count > limits.cards) return RestoreStatus::resource_limit;
        if (count > (r.bytes.size() - r.at) / 24) return RestoreStatus::malformed;
        std::vector<CardPool::Slot> slots(count);
        for (auto& slot : slots) {
            slot.generation = r.u32(); slot.live = r.boolean(); slot.value.definition = static_cast<CardId>(r.u8());
            slot.value.upgraded = r.boolean(); slot.value.creation_order = r.u32(); slot.value.origin.slot = r.u32(); slot.value.origin.generation = r.u32(); slot.value.last_finished_player_turn = r.i32(); slot.value.removed = r.boolean();
        }
        const auto extra_count = r.count(17, limits.cards);
        for (std::uint32_t i = 0; i < extra_count; ++i) {
            const auto card = r.handle(); auto data = r.card_extra();
            s_view.card_extra.push_back({card, data});
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
        const auto reaction_highest = r.u32();
        const auto reaction_count = r.count(7, limits.continuations);
        std::vector<ShuffleReactionPool::Slot> reactions(reaction_count);
        for (auto& slot : reactions) { slot.generation = r.u32(); slot.live = r.boolean(); slot.value.kind = static_cast<ShuffleReactionKind>(r.u8()); slot.value.fired = r.boolean(); }
        const auto reaction_free_count = r.count(4, reaction_count); std::vector<std::uint32_t> reaction_free(reaction_free_count);
        for (auto& index : reaction_free) index = r.u32();
        if (!s_view.shuffle_reactions.restore(std::move(reactions), std::move(reaction_free)) || reaction_highest < s_view.shuffle_reactions.data.highest_generation) return RestoreStatus::malformed;
        s_view.shuffle_reactions.data.highest_generation = reaction_highest;
        const auto order_count = r.count(8, reaction_count); for (std::uint32_t i = 0; i < order_count; ++i) s_view.shuffle_order.push_back(r.reaction_handle());
        const auto captured_count = r.count(8, std::uint64_t(limits.continuations) * 16); for (std::uint32_t i = 0; i < captured_count; ++i) s_view.captured_shuffle_reactions.push_back(r.reaction_handle());
        const auto operand_count = r.count(8, std::uint64_t(limits.continuations) * 16); for (std::uint32_t i = 0; i < operand_count; ++i) s_view.continuation_cards.push_back(r.handle());
        const auto frames = r.count(22, limits.continuations);
        for (std::uint32_t i = 0; i < frames; ++i) s_view.execution.push_back(r.frame());
        s_view.next_prompt.value = r.u32(); s_view.selection.id.value = r.u32();
        s_view.selection.effect = static_cast<SelectionEffect>(r.u8()); s_view.selection.zone = static_cast<Zone>(r.u8());
        s_view.selection.minimum = r.u32(); s_view.selection.maximum = r.u32(); s_view.selection.automatic_at_maximum = r.boolean(); s_view.selection.continuation_depth = r.u32();
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
        // Run restoration supplies its newly parsed canonical pool here. Borrow
        // only identity resolution; parsed records/overflow remain transactional.
        ConstCombatView validation = s.view();
        validation.creature_pool = destination.creature_pool;
        validation.creature_pool_owner = destination.creature_pool_owner;
        if (r.at != r.bytes.size() || !valid_combat(validation)) return RestoreStatus::malformed;
        destination.replace(std::move(s)); scratch.steps = 0;
        return RestoreStatus::restored;
    } catch (const std::bad_alloc&) { return RestoreStatus::resource_limit; } catch (const std::length_error&) { return RestoreStatus::resource_limit; }
    catch (...) { return RestoreStatus::malformed; }
}
} // namespace sts2::engine
