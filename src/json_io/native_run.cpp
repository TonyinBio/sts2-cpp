#include "sts2/json_io/native_run.hpp"
#include "sts2/engine/run.hpp"
#include <stdexcept>
#include <charconv>
#include <algorithm>

namespace sts2::engine {
void export_native_combat(nlohmann::json&, const RunState&, const RecordedCombat&);
namespace {
using Json = nlohmann::json;
constexpr std::array<const char*, 9> map_names{"unassigned", "unknown", "shop", "treasure", "rest_site", "monster", "elite", "boss", "ancient"};
constexpr std::array<const char*, 9> map_display_names{"Unassigned", "Unknown", "Shop", "Treasure", "RestSite", "Monster", "Elite", "Boss", "Ancient"};
Json native_float(float value) {
    std::array<char, 48> buffer; const auto result = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
    if (result.ec != std::errc{}) throw std::runtime_error("Cannot format native float");
    return Json::parse(buffer.data(), result.ptr);
}
Json names(std::span<const ContentId> ids) { Json out = Json::array(); for (auto id : ids) out.push_back(SetupRules::name(id)); return out; }
Json rng(const RngState& r) { return {{"counter", std::bit_cast<std::int32_t>(r.counter)}, {"s0", r.words[0]}, {"s1", r.words[1]}, {"s2", r.words[2]}, {"s3", r.words[3]}}; }
Json coords(const MapNode& n) { return {{"col", n.col}, {"row", n.row}}; }
Json map_node(const MapState& m, MapNodeId id) {
    const auto& n = m.nodes[id]; Json children = nullptr;
    if (n.children.size()) { children = Json::array(); for (auto c : n.children.values()) children.push_back(coords(m.nodes[c])); }
    return {{"can_modify", n.can_modify}, {"children", children}, {"coord", coords(n)}, {"type", map_names[static_cast<std::size_t>(n.kind)]}};
}
Json map_save(const MapState& m) {
    Json points = Json::array(), starts = Json::array();
    for (auto id : m.points()) points.push_back(map_node(m, id));
    for (auto id : m.starts.values()) starts.push_back(coords(m.nodes[id]));
    return {{"boss", map_node(m, m.boss)}, {"height", m.height}, {"points", points},
        {"second_boss", m.second_boss == no_node ? Json(nullptr) : map_node(m, m.second_boss)},
        {"start", map_node(m, m.start)}, {"start_coords", starts}, {"width", 7}};
}
Json relic_bag(ConstRelicBag bag) {
    constexpr std::array<const char*, 8> tiers{"common", "uncommon", "rare", "shop", "event", "ancient", "starter", "other"};
    Json out = Json::object();
    for (auto tier : bag.order) out[tiers[static_cast<std::size_t>(tier)]] = names(bag.tiers[static_cast<std::size_t>(tier)]);
    return {{"relic_id_lists", out}};
}

}
NativeRunExporter::NativeRunExporter() : defaults_(Json::parse(SetupRules::native_defaults())) {}
CombatTrace NativeRunExporter::trace() noexcept {
    return {this, [](void* context, ConstCombatView state, const CombatEvent& event) {
        static_cast<NativeRunExporter*>(context)->record(state, event);
    }};
}
void NativeRunExporter::record(ConstCombatView state, const CombatEvent& event) {
    if (event.kind == CombatEventKind::combat_started) {
        reset(); combat_.attached = true;
        combat_.card_generation = state.cards.data.highest_generation;
        return;
    }
    if (!combat_.attached) throw std::logic_error("Exporter must be attached at combat creation");
    if (event.kind == CombatEventKind::combat_updated) {
        combat_.boundary = state.boundary;
        return;
    }
    if (event.kind == CombatEventKind::combat_won) {
        combat_.history.clear(); combat_.plays.clear(); combat_.powers.clear();
        return;
    }
    if (event.kind == CombatEventKind::combat_lost) return;
    if (event.kind == CombatEventKind::power_received || event.kind == CombatEventKind::power_changed) {
        const PowerInstance* found = nullptr;
        for (const auto* creature : {&state.player, &state.enemy})
            for (auto id : creature->power_ids()) {
                const auto& power = creature->power(id);
                if (power.instance == event.object) found = &power;
            }
        if (!found || event.object > combat_.powers.size()) throw std::logic_error("Missing exported power identity");
        if (event.object == combat_.powers.size()) combat_.powers.push_back(*found);
        else combat_.powers[event.object] = *found;
        if (event.kind == CombatEventKind::power_changed) return;
    }
    if (event.kind == CombatEventKind::play_started) {
        const auto& frame = state.execution.back();
        if (frame.play != combat_.plays.size()) throw std::logic_error("Missing exported play identity");
        combat_.plays.push_back({frame.source, frame.energy, frame.enemy_target});
    }
    combat_.history.push_back(event);
}
Json NativeRunExporter::snapshot(const RunState& s) const {
    auto s_view = s.view();
    if (!valid_run(s))
        throw std::invalid_argument("Cannot export an uninitialized or invalid run");
    if (!s_view.maps[0] || s_view.current == no_node) throw std::invalid_argument("Run has no initialized map");
    const auto& setup = s_view; const auto& map = *s_view.maps[0];
    const auto* character = SetupRules::character(setup.character);
    if (!character) throw std::invalid_argument("Unknown run character");
    Json acts = Json::array();
    for (std::size_t i = 0; i < setup.acts.size(); ++i) {
        const auto& a = setup.acts[i];
        acts.push_back({{"id", SetupRules::name(a.act)}, {"rooms", {
            {"ancient_id", SetupRules::name(a.ancient)}, {"boss_encounters_visited", a.bosses_visited},
            {"boss_id", SetupRules::name(a.boss)}, {"elite_encounter_ids", names(a.elites)}, {"elite_encounters_visited", a.elites_visited},
            {"event_ids", names(a.events)}, {"events_visited", a.events_visited},
            {"normal_encounter_ids", names(a.normal)}, {"normal_encounters_visited", a.normal_visited},
            {"second_boss_id", a.second_boss == no_content ? Json(nullptr) : Json(SetupRules::name(a.second_boss))}}},
            {"saved_map", s_view.maps[i] ? map_save(*s_view.maps[i]) : Json(nullptr)}});
    }
    Json cards = Json::array(), relics = Json::array();
    for (const auto& card : setup.deck) {
        auto value = defaults_.at("cards").at(std::string(SetupRules::name(card.definition)));
        value["current_upgrade_level"] = card.upgrade; value["floor_added_to_deck"] = card.floor_added < 0 ? Json(nullptr) : Json(card.floor_added);
        cards.push_back(std::move(value));
    }
    for (auto id : setup.relics) {
        auto value = defaults_.at("relics").at(std::string(SetupRules::name(id)));
        if (SetupRules::name(id) == "RELIC.JOSS_PAPER" && s_view.joss_paper_counter != 0)
            value["props"] = {{"bools", nullptr}, {"card_arrays", nullptr}, {"cards", nullptr}, {"int_arrays", nullptr},
                {"ints", Json::array({{{"name", "CardsExhausted"}, {"value", s_view.joss_paper_counter}}})},
                {"model_ids", nullptr}, {"strings", nullptr}};
        value["floor_added_to_deck"] = 1; relics.push_back(std::move(value));
    }
    Json player_streams = Json::object(), run_streams = Json::object();
    constexpr std::array<const char*, 3> player_names{"rewards", "shops", "transformations"};
    for (std::size_t i = 0; i < player_names.size(); ++i) player_streams[player_names[i]] = rng(setup.player_rng.streams[i].state());
    for (std::size_t i = 0; i < run_stream_names.size(); ++i) run_streams[std::string(run_stream_names[i])] = rng(setup.rng.streams[i].state());
    Json potions = Json::array();
    for (std::size_t i = 0; i < setup.potions.size(); ++i) if (setup.potions[i] != no_content) {
        auto value = defaults_.at("potions").at(std::string(SetupRules::name(setup.potions[i]))); value["slot_index"] = i; potions.push_back(value);
    }
    Json player = {{"base_orb_slot_count", setup.orb_slots}, {"character_id", SetupRules::name(setup.character)},
        {"current_hp", setup.hp}, {"deck", cards}, {"extra_fields", {{"card_shop_removals_used", 0}, {"ccccombo_badge_unlocked", false}, {"wongo_points", 0}}},
        {"gold", setup.gold}, {"max_energy", character->energy}, {"max_hp", setup.max_hp}, {"max_potion_slot_count", setup.potion_slots},
        {"net_id", 1}, {"odds", {{"card_rarity_odds_value", native_float(setup.card_rarity_offset)}, {"potion_reward_odds_value", native_float(setup.potion_reward_odds)}}},
        {"potions", potions}, {"relic_grab_bag", relic_bag(setup.player_relics)}, {"relics", relics},
        {"rng", {{"rngs", player_streams}, {"seed", game_hash(setup.seed)}}}, {"unlock_state", defaults_.at("unlock_state")}};
    for (const char* field : {"discovered_cards", "discovered_epochs", "discovered_relics"}) player[field] = Json::array();
    if (s_view.selected_neow != no_content) player["discovered_relics"].push_back(SetupRules::name(s_view.selected_neow));
    Json room;
    if (s_view.phase == RunPhase::map) {
        Json choices = Json::array();
        for (auto child : map.nodes[s_view.current].children.values()) {
            const auto& n = map.nodes[child]; choices.push_back({{"col", n.col}, {"row", n.row}, {"point_type", map_display_names[static_cast<std::size_t>(n.kind)]}});
        }
        room = {{"kind", "map"}, {"current_coord", coords(map.nodes[s_view.current])}, {"choices", choices}};
    } else {
        Json options = Json::array();
        if (s_view.phase == RunPhase::neow) for (auto id : s_view.neow_options) {
            auto offered = defaults_.at("relics").at(std::string(SetupRules::name(id))); offered["floor_added_to_deck"] = 0;
            options.push_back({
            {"key", "NEOW.pages.INITIAL.options." + std::string(SetupRules::name(id).substr(6))},
            {"is_locked", false}, {"is_proceed", false}, {"was_chosen", false},
            {"relic", offered}});
        }
        room = {{"kind", "event"}, {"id", "EVENT.NEOW"}, {"finished", s_view.phase == RunPhase::neow_done},
            {"page", s_view.phase == RunPhase::neow ? "NEOW.pages.INITIAL.description" : "NEOW.pages.DONE.description"},
            {"options", options}, {"dynamic_vars", Json::object()}, {"string_vars", Json::object()}, {"rng", rng(s_view.event_rng.state())}};
    }
    Json visited = Json::array(); for (auto id : s_view.visited) visited.push_back(coords(map.nodes[id]));
    const auto native_room = Json{{"encounter_id", nullptr}, {"encounter_state", Json::object()},
        {"event_id", s_view.phase == RunPhase::map ? Json(nullptr) : Json("EVENT.NEOW")}, {"extra_rewards", Json::object()},
        {"is_pre_finished", s_view.phase == RunPhase::neow_done}, {"parent_event_id", nullptr}, {"reward_proportion", 0},
        {"room_type", s_view.phase == RunPhase::map ? "map" : "event"}, {"should_resume_parent_event", true}};
    Json out = {{"acts", acts}, {"ascension", setup.ascension}, {"current_act_index", s_view.act_index}, {"dailyTime", nullptr},
        {"events_seen", Json::array()}, {"extra_fields", {{"freed_repy", false}, {"started_with_neow", true}, {"test_subject_kills", 0}}},
        {"game_mode", "standard"}, {"map_drawings", nullptr}, {"comparison_revision", 2},
        {"modifiers", Json::array()}, {"num_reloads", 0}, {"odds", {{"unknown_map_point_elite_odds_value", -1},
            {"unknown_map_point_monster_odds_value", 0.1}, {"unknown_map_point_shop_odds_value", 0.03}, {"unknown_map_point_treasure_odds_value", 0.02}}},
        {"platform_type", "none"}, {"pre_finished_room", nullptr}, {"players", Json::array({player})}, {"rng", {{"rngs", run_streams}, {"seed", std::string_view(setup.seed)}}},
        {"run_time", s_view.clock.elapsed_seconds()}, {"save_time", s_view.clock.now_seconds()}, {"schema_version", 20},
        {"shared_relic_grab_bag", relic_bag(setup.shared_relics)}, {"start_time", s_view.clock.config().epoch_seconds},
        {"visited_map_coords", visited}, {"win_time", 0}, {"boundary_kind", "awaiting_input"}, {"run_ending_reason", nullptr},
        {"current_room", native_room}, {"native_model_state", Json::object()}, {"combat", nullptr}, {"pending_choice", nullptr},
        {"run_flow", {{"room", room}, {"pending_choice", nullptr}, {"reward_sets", Json::array()}, {"operations", Json::array()}, {"combat_rewards_offered", false}}}};
    if (s_view.combat || s_view.phase == RunPhase::combat_done || s_view.phase == RunPhase::rewards || s_view.phase == RunPhase::complete) export_native_combat(out, s, combat_);
    return out;
}
} // namespace sts2::engine
