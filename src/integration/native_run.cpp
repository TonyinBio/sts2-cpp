#include "sts2/integration/native_run.hpp"
#include "sts2/kernel/run_simulator.hpp"
#include <stdexcept>
#include <charconv>
#include <algorithm>

namespace sts2::engine {
void export_native_combat(nlohmann::json&, const RunState&);
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
Json relic_bag(const RelicBag& bag) {
    constexpr std::array<const char*, 8> tiers{"common", "uncommon", "rare", "shop", "event", "ancient", "starter", "other"};
    Json out = Json::object();
    for (auto tier : bag.order) out[tiers[static_cast<std::size_t>(tier)]] = names(bag.tiers[static_cast<std::size_t>(tier)]);
    return {{"relic_id_lists", out}};
}
Json stats(const RunState& s, bool ancient = true) {
    Json out = {{"player_id", 1}, {"current_gold", 0}, {"current_hp", 0}, {"damage_taken", 0},
        {"gold_gained", ancient ? s.gold_gained : 0}, {"gold_lost", 0}, {"gold_spent", 0}, {"gold_stolen", 0},
        {"hp_healed", ancient ? s.initial_heal : 0}, {"is_affected_by_fur_coat", false}, {"max_hp", 0},
        {"max_hp_gained", 0}, {"max_hp_lost", 0}, {"stolen_loot", 0}};
    for (const char* field : {"ancient_choice", "bought_colorless", "bought_potions", "bought_relics", "card_choices",
        "cards_enchanted", "cards_gained", "cards_removed", "cards_transformed", "completed_quests", "downgraded_cards",
        "event_choices", "potion_choices", "potion_discarded", "potion_used", "relic_choices", "relics_removed",
        "rest_site_choices", "upgraded_cards"}) out[field] = Json::array();
    if (ancient && s.selected_neow != no_content) {
        for (auto id : s.neow_options) {
            const auto name = SetupRules::name(id).substr(6);
            out["ancient_choice"].push_back({{"TextKey", name}, {"title", {{"key", "NEOW.pages.INITIAL.options." + std::string(name) + ".title"}, {"table", "ancients"}}}, {"was_chosen", id == s.selected_neow}});
        }
        out["relic_choices"].push_back({{"choice", SetupRules::name(s.selected_neow)}, {"was_picked", true}});
    }
    return out;
}
}
NativeRunExporter::NativeRunExporter() : defaults_(Json::parse(SetupRules::native_defaults())) {}
Json NativeRunExporter::snapshot(const RunState& s) const {
    if (!valid_run(s))
        throw std::invalid_argument("Cannot export an uninitialized or invalid run");
    if (s.phase != RunPhase::neow && s.phase != RunPhase::neow_done && s.phase != RunPhase::map && !s.combat)
        throw std::invalid_argument("Full native export is unsupported at this run phase");
    if (!s.maps[0] || s.current == no_node) throw std::invalid_argument("Run has no initialized map");
    const auto& setup = s.setup; const auto& map = *s.maps[0];
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
            {"saved_map", s.maps[i] ? map_save(*s.maps[i]) : Json(nullptr)}});
    }
    Json cards = Json::array(), relics = Json::array();
    for (const auto& card : setup.deck) {
        auto value = defaults_.at("cards").at(std::string(SetupRules::name(card.definition)));
        value["current_upgrade_level"] = card.upgrade; value["floor_added_to_deck"] = card.floor_added < 0 ? Json(nullptr) : Json(card.floor_added);
        cards.push_back(std::move(value));
    }
    for (auto id : setup.relics) {
        auto value = defaults_.at("relics").at(std::string(SetupRules::name(id)));
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
        {"current_hp", setup.hp}, {"deck", cards}, {"extra_fields", {{"card_shop_removals_used", 0}, {"ccccombo_badge_unlocked", false}, {"damage_dealt", 0}, {"debuffs_applied", 0}, {"wongo_points", 0}}},
        {"gold", setup.gold}, {"max_energy", character->energy}, {"max_hp", setup.max_hp}, {"max_potion_slot_count", setup.potion_slots},
        {"net_id", 1}, {"odds", {{"card_rarity_odds_value", native_float(setup.card_rarity_offset)}, {"potion_reward_odds_value", native_float(setup.potion_reward_odds)}}},
        {"potions", potions}, {"relic_grab_bag", relic_bag(setup.player_relics)}, {"relics", relics},
        {"rng", {{"rngs", player_streams}, {"seed", game_hash(setup.seed)}}}, {"unlock_state", defaults_.at("unlock_state")}};
    for (const char* field : {"discovered_cards", "discovered_enemies", "discovered_epochs", "discovered_potions", "discovered_relics"}) player[field] = Json::array();
    if (s.selected_neow != no_content) player["discovered_relics"].push_back(SetupRules::name(s.selected_neow));
    Json room;
    if (s.phase == RunPhase::map) {
        Json choices = Json::array();
        for (auto child : map.nodes[s.current].children.values()) {
            const auto& n = map.nodes[child]; choices.push_back({{"col", n.col}, {"row", n.row}, {"point_type", map_display_names[static_cast<std::size_t>(n.kind)]}});
        }
        room = {{"kind", "map"}, {"current_coord", coords(map.nodes[s.current])}, {"choices", choices}};
    } else {
        Json options = Json::array();
        if (s.phase == RunPhase::neow) for (auto id : s.neow_options) {
            auto offered = defaults_.at("relics").at(std::string(SetupRules::name(id))); offered["floor_added_to_deck"] = 0;
            options.push_back({
            {"key", "NEOW.pages.INITIAL.options." + std::string(SetupRules::name(id).substr(6))},
            {"is_locked", false}, {"is_proceed", false}, {"was_chosen", false},
            {"relic", offered}});
        }
        room = {{"kind", "event"}, {"id", "EVENT.NEOW"}, {"finished", s.phase == RunPhase::neow_done},
            {"page", s.phase == RunPhase::neow ? "NEOW.pages.INITIAL.description" : "NEOW.pages.DONE.description"},
            {"options", options}, {"dynamic_vars", Json::object()}, {"string_vars", Json::object()}, {"rng", rng(s.event_rng.state())}};
    }
    Json visited = Json::array(); for (auto id : s.visited) visited.push_back(coords(map.nodes[id]));
    const auto native_room = Json{{"encounter_id", nullptr}, {"encounter_state", Json::object()},
        {"event_id", s.phase == RunPhase::map ? Json(nullptr) : Json("EVENT.NEOW")}, {"extra_rewards", Json::object()},
        {"is_pre_finished", s.phase == RunPhase::neow_done}, {"parent_event_id", nullptr}, {"reward_proportion", 0},
        {"room_type", s.phase == RunPhase::map ? "map" : "event"}, {"should_resume_parent_event", true}};
    const Json history_room = {{"model_id", "EVENT.NEOW"}, {"monster_ids", Json::array()}, {"room_type", "event"}, {"turns_taken", 0}};
    const Json history_entry = {{"map_point_type", "ancient"}, {"player_stats", Json::array({stats(s)})}, {"rooms", Json::array({history_room})}};
    Json history = Json::array({Json::array({history_entry})});
    if (s.combat && !s.direct_combat) history[0].push_back({{"map_point_type", map_names[static_cast<std::size_t>(map.nodes[s.current].kind)]},
        {"player_stats", Json::array({stats(s, false)})}, {"rooms", Json::array({{{"model_id", "ENCOUNTER.SHRINKER_BEETLE_WEAK"},
            {"monster_ids", Json::array()}, {"room_type", "monster"}, {"turns_taken", 0}}})}});
    Json out = {{"acts", acts}, {"ascension", setup.ascension}, {"current_act_index", s.act_index}, {"dailyTime", nullptr},
        {"events_seen", Json::array()}, {"extra_fields", {{"freed_repy", false}, {"started_with_neow", true}, {"test_subject_kills", 0}}},
        {"game_mode", "standard"}, {"map_drawings", nullptr}, {"map_point_history", history},
        {"modifiers", Json::array()}, {"num_reloads", 0}, {"odds", {{"unknown_map_point_elite_odds_value", -1},
            {"unknown_map_point_monster_odds_value", 0.1}, {"unknown_map_point_shop_odds_value", 0.03}, {"unknown_map_point_treasure_odds_value", 0.02}}},
        {"platform_type", "none"}, {"pre_finished_room", nullptr}, {"players", Json::array({player})}, {"rng", {{"rngs", run_streams}, {"seed", setup.seed}}},
        {"run_time", s.clock.elapsed_seconds()}, {"save_time", s.clock.now_seconds()}, {"schema_version", 20},
        {"shared_relic_grab_bag", relic_bag(setup.shared_relics)}, {"start_time", s.clock.config().epoch_seconds},
        {"visited_map_coords", visited}, {"win_time", 0}, {"boundary_kind", "awaiting_input"}, {"run_ending_reason", nullptr},
        {"current_room", native_room}, {"native_model_state", Json::object()}, {"combat", nullptr}, {"pending_choice", nullptr},
        {"run_flow", {{"room", room}, {"pending_choice", nullptr}, {"reward_sets", Json::array()}, {"operations", Json::array()}, {"combat_rewards_offered", false}}}};
    if (s.combat) export_native_combat(out, s);
    if (!s.encounters.empty()) {
        // Completed combat graphs retain live aliases for diagnostic history;
        // the native floor history contains captured value descriptors instead.
        Json history = Json::array({out["map_point_history"][0][0]});
        std::uint32_t floor = 0;
        auto append = [&](const Json& entry, std::uint32_t entry_floor) {
            if (entry_floor != floor) { history.push_back(entry); floor = entry_floor; return; }
            auto& target = history.back();
            // Native EnterRoom replaces the active room without adding a new
            // floor/room-history record. Spawn IDs append; terminal turns replace
            // the previous room's value, while an unfinished fight preserves it.
            for (const auto& room : entry["rooms"]) {
                auto& retained = target["rooms"].back();
                for (const auto& monster : room["monster_ids"]) retained["monster_ids"].push_back(monster);
                if (room["turns_taken"] != 0) retained["turns_taken"] = room["turns_taken"];
            }
            auto& stats = target["player_stats"][0];
            for (const auto& [key, value] : entry["player_stats"][0].items()) {
                if (value.is_array()) for (const auto& item : value) stats[key].push_back(item);
                else if (value.is_number() && key != "player_id") stats[key] = stats[key].get<std::int64_t>() + value.get<std::int64_t>();
            }
        };
        auto& player = out["players"][0];
        for (const auto& entry : s.encounters) {
            RunState historical = s; historical.encounters.clear();
            historical.combat = entry.combat; historical.rewards = entry.rewards; historical.discarded_potions = entry.discarded_potions;
            historical.setup.deck = entry.deck; historical.setup.hp = entry.combat.player.hp;
            historical.setup.rng = entry.combat.rng; historical.current = entry.node; historical.visited.resize(entry.floor);
            historical.boundary = entry.combat.boundary; historical.clock = entry.combat.clock;
            historical.phase = RunPhase::map; historical.direct_combat = false;
            Json previous = out;
            previous["map_point_history"] = Json::array({Json::array({out["map_point_history"][0][0], {
                {"map_point_type", map_names[static_cast<std::size_t>(map.nodes[entry.node].kind)]},
                {"player_stats", Json::array({stats(historical, false)})},
                {"rooms", Json::array({{{"model_id", "ENCOUNTER.SHRINKER_BEETLE_WEAK"},
                    {"monster_ids", Json::array()}, {"room_type", "monster"}, {"turns_taken", 0}}})}
            }})});
            previous["players"][0]["discovered_enemies"] = Json::array();
            previous["players"][0]["discovered_potions"] = Json::array();
            export_native_combat(previous, historical);
            append(previous["map_point_history"][0].back(), entry.floor);
            for (const auto* field : {"discovered_enemies", "discovered_potions"}) for (const auto& item : previous["players"][0][field])
                if (std::find(player[field].begin(), player[field].end(), item) == player[field].end()) player[field].push_back(item);
            for (const auto* field : {"damage_dealt", "debuffs_applied"})
                player["extra_fields"][field] = player["extra_fields"][field].get<std::int32_t>() + previous["players"][0]["extra_fields"][field].get<std::int32_t>();
        }
        if (s.combat) append(out["map_point_history"][0].back(), static_cast<std::uint32_t>(s.visited.size()));
        out["map_point_history"] = Json::array({std::move(history)});
    }
    return out;
}
} // namespace sts2::engine
