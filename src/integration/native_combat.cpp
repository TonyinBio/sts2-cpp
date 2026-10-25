#include "sts2/integration/native_run.hpp"
#include <bit>
#include <algorithm>
#include <stdexcept>

namespace sts2::engine {
namespace {
using Json = nlohmann::json;
constexpr const char* player = "/combat/creatures/player_0";
constexpr const char* enemy = "/combat/creatures/enemy_0";
const char* creature(bool is_enemy) { return is_enemy ? enemy : player; }
std::string card_ref(CardHandle h) { return "/combat/cards/card_" + std::to_string(h.slot); }
std::string play_ref(std::uint32_t id) { return "/combat/card_plays/" + std::to_string(id); }
std::string power_ref(std::size_t id) { return "/combat/powers/" + std::to_string(id); }
Json random_state(const RngState& r) {
    return {{"counter", std::bit_cast<std::int32_t>(r.counter)}, {"s0", r.words[0]}, {"s1", r.words[1]}, {"s2", r.words[2]}, {"s3", r.words[3]}};
}
Json variables(const CardInstance& c) {
    switch (c.definition) {
    case CardId::strike: return {{"Damage", c.upgraded ? 9 : 6}};
    case CardId::defend: return {{"Block", c.upgraded ? 8 : 5}};
    case CardId::bash: return {{"Damage", c.upgraded ? 10 : 8}, {"VulnerablePower", c.upgraded ? 3 : 2}};
    case CardId::survivor: return {{"Block", c.upgraded ? 11 : 8}};
    case CardId::purity: return {{"Cards", c.upgraded ? 5 : 3}};
    case CardId::armaments: return {{"Block", 5}};
    case CardId::headbutt: return {{"Damage", c.upgraded ? 12 : 9}};
    case CardId::burning_pact: return {{"Cards", c.upgraded ? 3 : 2}};
    case CardId::true_grit: return {{"Block", c.upgraded ? 9 : 7}};
    case CardId::count: break;
    }
    throw std::invalid_argument("Unsupported card variables");
}
Json damage_result(const HitResult& h, bool target_enemy) {
    return {{"receiver", creature(target_enemy)}, {"props", 8}, {"blocked_damage", h.blocked}, {"unblocked_damage", h.unblocked},
        {"overkill_damage", h.overkill}, {"was_block_broken", h.block_broken}, {"was_fully_blocked", h.fully_blocked}, {"was_target_killed", h.killed}};
}
}
void export_native_combat(Json& out, const RunState& run) {
    if (!run.combat) throw std::invalid_argument("Unsupported combat export context");
    const auto map_room = out["current_room"], map_flow = out["run_flow"];
    const auto& s = *run.combat;
    const bool won = s.phase == Phase::combat_won, died = s.phase == Phase::player_died, terminal = won || died;
    const bool choosing = s.phase == Phase::discard_choice || s.phase == Phase::card_choice;
    Json piles = Json::object(); constexpr std::array<const char*, 5> zone_names{"Hand", "Draw", "Discard", "Exhaust", "Play"};
    for (std::size_t i = 0; i < s.zones.size(); ++i) {
        Json list = Json::array(); for (auto h : s.zones[i]) list.push_back(card_ref(h)); piles[zone_names[i]] = std::move(list);
    }
    Json combat = {{"round_number", s.turn}, {"current_side", died ? "Enemy" : "Player"}, {"turn_control", {
        {"is_in_progress", !terminal}, {"is_starting", false}, {"is_enemy_turn_started", died},
        {"ending_player_turn_phase_one", false}, {"ending_player_turn_phase_two", false}, {"has_pending_loss", false},
        {"player_actions_disabled", died}, {"card_or_potion_effect_depth", choosing ? 1 : 0},
        {"players_taking_extra_turn", Json::array()}, {"players_ready_to_end_turn", died ? Json::array({"/players/0"}) : Json::array()}, {"players_ready_to_begin_enemy_turn", died ? Json::array({"/players/0"}) : Json::array()}}},
        {"allies", Json::array({player})}, {"enemies", won ? Json::array() : Json::array({enemy})}, {"escaped_creatures", Json::array()},
        {"spawned_enemy_ids", Json::array({"MONSTER.SHRINKER_BEETLE"})}, {"encounter_fields", Json::object()},
        {"players", Json::array({{{"player", "/players/0"}, {"creature", player}, {"turn_number", s.turn}, {"phase", terminal ? "None" : "Play"},
            {"energy", s.energy}, {"stars", 0}, {"can_use_or_remove_potions", true}, {"is_active_for_hooks", !died},
            {"piles", piles}, {"pets", Json::array()}, {"orb_capacity", 0}, {"orbs", Json::array()}}})}};
    for (const char* field : {"creatures", "cards", "powers", "card_plays", "orbs", "afflictions", "attacks", "retained_inventory_models"}) combat[field] = Json::object();
    combat["active_card_plays"] = Json::array(); combat["history"] = Json::array();
    auto make_creature = [](bool is_enemy, int block) {
        return Json{{"player", is_enemy ? Json(nullptr) : Json("/players/0")}, {"monster", nullptr}, {"side", is_enemy ? "Enemy" : "Player"},
            {"block", block}, {"hp_is_infinite", false}, {"slot_name", nullptr}, {"pet_owner", nullptr}, {"powers", Json::array()}};
    };
    combat["creatures"]["player_0"] = make_creature(false, s.player.block);
    if (!won) {
        auto e = make_creature(true, s.enemy.block);
        constexpr std::array<const char*, 3> moves{"SHRINKER_MOVE", "CHOMP_MOVE", "STOMP_MOVE"};
        Json move_states = Json::object(), log = Json::array();
        for (unsigned i = 0; i < moves.size(); ++i) move_states[moves[i]] = {{"performed_at_least_once", died && i == static_cast<unsigned>(s.move)}, {"must_perform_once_before_transitioning", false}, {"follow_up_state", moves[i == 1 ? 2 : 1]}};
        for (int turn = 1; turn <= s.turn; ++turn) log.push_back(moves[turn == 1 ? 0 : (turn % 2 == 0 ? 1 : 2)]);
        const auto& map = *run.maps[run.act_index]; const auto& coord = map.nodes[run.current];
        const GameRng monster_rng(s.numeric_seed + static_cast<std::uint64_t>(coord.col + coord.row + run.act_index + 1));
        e["monster"] = {{"id", "MONSTER.SHRINKER_BEETLE"}, {"current_hp", s.enemy.hp}, {"max_hp", s.enemy.max_hp},
            {"spawned_this_turn", s.turn == 1}, {"is_performing_move", false}, {"rng", random_state(monster_rng.state())},
            {"ai", {{"current_state", moves[static_cast<std::size_t>(s.move)]}, {"initial_state", moves[0]},
                {"next_move", moves[static_cast<std::size_t>(s.move)]}, {"state_log", log}, {"performed_first_move", s.turn > 1}, {"move_states", move_states}}}, {"model_fields", Json::object()}};
        combat["creatures"]["enemy_0"] = std::move(e);
    }
    for (std::size_t i = 0; i < s.cards.slots().size(); ++i) {
        const auto& slot = s.cards.slots()[i]; if (!slot.live) continue;
        const auto& card = slot.value;
        const auto linked = std::find_if(run.setup.deck.begin(), run.setup.deck.end(), [&](const auto& c) { return c.instance == card.deck_instance; });
        const auto deck_index = card.deck_instance == UINT32_MAX ? card.creation_order : static_cast<std::uint32_t>(linked - run.setup.deck.begin());
        if (deck_index >= run.setup.deck.size()) throw std::invalid_argument("Missing permanent card relationship");
        const auto vars = variables(card);
        auto permanent = card; permanent.upgraded = run.setup.deck[deck_index].upgrade != 0;
        out["native_model_state"]["/players/0/deck/" + std::to_string(deck_index)] = {{"model_fields", Json::object()}, {"dynamic_vars", variables(permanent)}};
        if (won) continue;
        combat["cards"]["card_" + std::to_string(i)] = {{"saved", out["players"][0]["deck"][deck_index]},
            {"owner", "/players/0"}, {"deck_version", "/players/0/deck/" + std::to_string(deck_index)}, {"clone_of", nullptr},
            {"is_dupe", false}, {"base_replay_count", 0}, {"local_keywords", card.definition == CardId::purity ? Json::array({"Retain", "Exhaust"}) : Json::array()}, {"exhaust_on_next_play", false},
            {"has_single_turn_retain", false}, {"has_single_turn_sly", false}, {"has_been_removed_from_state", false},
            {"dynamic_vars", vars}, {"energy_cost", {{"base", Ruleset::pinned().card(card.definition)->cost}, {"captured_x_value", 0}, {"local_modifiers", Json::array()}}},
            {"base_star_cost", -1}, {"temporary_star_costs", Json::array()}, {"last_stars_spent", 0}, {"affliction", nullptr}, {"enchantment_runtime", nullptr}, {"model_fields", Json::object()}};
    }
    if (!won) for (std::size_t i = 0; i < s.cards.slots().size(); ++i) {
        const auto& slot = s.cards.slots()[i]; if (!slot.live) continue;
        combat["cards"]["card_" + std::to_string(i)]["saved"]["current_upgrade_level"] = slot.value.upgraded ? 1 : 0;
    }
    for (std::size_t i = 0; i < run.setup.relics.size(); ++i) {
        const auto name = SetupRules::name(run.setup.relics[i]);
        const auto vars = name == "RELIC.BURNING_BLOOD" ? Json{{"Heal", 6}} : name == "RELIC.GOLDEN_PEARL" ? Json{{"Gold", 150}} : Json::object();
        out["native_model_state"]["/players/0/relics/" + std::to_string(i)] = {{"model_fields", Json::object()}, {"dynamic_vars", vars}};
    }
    if (!terminal) for (std::size_t i = 0; i < run.setup.potions.size(); ++i) {
        if (run.setup.potions[i] == no_content) continue;
        if (SetupRules::name(run.setup.potions[i]) != "POTION.POTION_OF_BINDING") throw std::invalid_argument("Unsupported active potion metadata");
        out["native_model_state"]["/players/0/potions/" + std::to_string(i)] = {
            {"model_fields", Json::object()}, {"dynamic_vars", {{"VulnerablePower", 1}, {"WeakPower", 1}}}};
    }
    for (std::size_t i = 0; i < s.powers.size(); ++i) {
        const auto& p = s.powers[i]; const bool shrink = p.definition == PowerId::shrink;
        Json value = {{"id", shrink ? "POWER.SHRINK_POWER" : "POWER.VULNERABLE_POWER"}, {"amount", p.amount},
            {"amount_on_turn_start", p.amount_on_turn_start}, {"skip_next_duration_tick", p.skip_next_tick},
            {"applier", creature(!p.enemy_owner)}, {"target", nullptr}, {"dynamic_vars", shrink ? Json{{"DamageDecrease", 30}} : Json{{"DamageIncrease", 1.5}}}, {"model_fields", Json::object()}};
        if (p.active) combat["creatures"][p.enemy_owner ? "enemy_0" : "player_0"]["powers"].push_back(power_ref(i));
        else value["detached_owner"] = creature(p.enemy_owner);
        combat["powers"][std::to_string(i)] = std::move(value);
    }
    for (std::size_t i = 0; i < s.plays.size(); ++i) {
        const auto& p = s.plays[i];
        combat["card_plays"][std::to_string(i)] = {{"card", card_ref(p.card)}, {"player", "/players/0"}, {"target", p.enemy_target ? Json(enemy) : Json(nullptr)},
            {"result_pile", s.cards.get(p.card)->definition == CardId::purity ? "Exhaust" : "Discard"}, {"resources", {{"energy_spent", p.energy}, {"energy_value", p.energy}, {"stars_spent", 0}, {"star_value", 0}}},
            {"is_auto_play", false}, {"play_index", 0}, {"play_count", 1}};
    }
    constexpr std::array<const char*, 10> kinds{"CardDrawnEntry", "CardDiscardedEntry", "EnergySpentEntry", "CardPlayStartedEntry", "CardPlayFinishedEntry", "DamageReceivedEntry", "CreatureAttackedEntry", "BlockGainedEntry", "PowerReceivedEntry", "CardExhaustedEntry"};
    for (const auto& h : s.history) {
        Json entry = {{"kind", kinds[static_cast<std::size_t>(h.kind)]}, {"actor", creature(h.actor_enemy)}, {"round_number", h.turn},
            {"current_side", h.enemy_side ? "Enemy" : "Player"}, {"player_turn_numbers", Json::array({{{"player", "/players/0"}, {"turn_number", h.turn}}})}};
        switch (h.kind) {
        case HistoryKind::drawn: entry["card"] = card_ref(h.card); entry["from_hand_draw"] = true; break;
        case HistoryKind::exhausted: case HistoryKind::discarded: entry["card"] = card_ref(h.card); break;
        case HistoryKind::energy_spent: entry["amount"] = h.amount; break;
        case HistoryKind::play_started: case HistoryKind::play_finished:
            entry["card_play"] = play_ref(h.object); if (h.kind == HistoryKind::play_finished) entry["was_ethereal"] = false; break;
        case HistoryKind::damaged:
            entry["result"] = damage_result(h.hit, h.actor_enemy); entry["dealer"] = creature(!h.actor_enemy);
            entry["card_source"] = h.card.present() ? Json(card_ref(h.card)) : Json(nullptr); break;
        case HistoryKind::attacked: entry["damage_results"] = Json::array({damage_result(h.hit, !h.actor_enemy)}); break;
        case HistoryKind::block_gained: entry["amount"] = h.amount; entry["props"] = 8; entry["card_play"] = play_ref(h.object); break;
        case HistoryKind::power_received: entry["amount"] = h.amount; entry["power"] = power_ref(h.object); entry["applier"] = h.amount < 0 && s.powers[h.object].definition == PowerId::vulnerable ? Json(nullptr) : Json(creature(!h.actor_enemy)); break;
        }
        combat["history"].push_back(std::move(entry));
    }
    if (choosing) {
        const auto& frame = s.execution.back();
        combat["active_card_plays"].push_back({{"card_play", play_ref(frame.play)},
            {"result_location", {{"player", "/players/0"}, {"pile_type", s.cards.get(frame.source)->definition == CardId::purity ? "Exhaust" : "Discard"}, {"position", "Bottom"}}}});
        Json candidates = Json::array();
        const auto& offered = s.phase == Phase::card_choice ? s.selection.candidates : s.zone(Zone::hand);
        for (auto h : offered) candidates.push_back(card_ref(h));
        out["pending_choice"] = {{"player", "/players/0"}, {"sources", Json::array({card_ref(frame.source)})},
            {"candidates", candidates}, {"min_select", s.phase == Phase::card_choice ? s.selection.minimum : 1}, {"max_select", s.phase == Phase::card_choice ? s.selection.maximum : 1}, {"require_manual_confirmation", s.cards.get(frame.source)->definition == CardId::purity}, {"cancelable", false}};
    }
    out["combat"] = std::move(combat); out["run_flow"] = nullptr;
    out["current_room"] = {{"encounter_id", "ENCOUNTER.SHRINKER_BEETLE_WEAK"}, {"encounter_state", Json::object()}, {"event_id", nullptr},
        {"extra_rewards", Json::object()}, {"is_pre_finished", won}, {"parent_event_id", nullptr}, {"reward_proportion", 1}, {"room_type", "monster"}, {"should_resume_parent_event", true}};
    const auto history_index = run.direct_combat ? 0 : out["map_point_history"][0].size() - 1;
    auto& stats = out["map_point_history"][0][history_index]["player_stats"][0];
    stats["damage_taken"] = s.damage_taken; stats["hp_healed"] = (run.direct_combat ? run.initial_heal : 0) + s.healing;
    auto& room = out["map_point_history"][0][history_index]["rooms"].back();
    room["monster_ids"] = Json::array({"MONSTER.SHRINKER_BEETLE"}); room["turns_taken"] = (won || (!run.direct_combat && died)) ? s.turn : 0;
    out["players"][0]["extra_fields"]["damage_dealt"] = s.damage_dealt;
    out["players"][0]["extra_fields"]["debuffs_applied"] = s.debuffs_applied;
    if (won || (!run.direct_combat && died)) out["players"][0]["discovered_enemies"].push_back("MONSTER.SHRINKER_BEETLE");

    if (!run.direct_combat && terminal) {
        out["combat"] = nullptr; out["native_model_state"] = Json::object();
        out["run_flow"] = died ? Json(nullptr) : Json{{"room", {{"kind", "combat_end"}}}, {"pending_choice", nullptr},
            {"reward_sets", Json::array()}, {"operations", Json::array()}, {"combat_rewards_offered", false}};
    }
    if (run.rewards) {
        const auto& rewards = *run.rewards;
        auto saved_reward = [](const char* type, int gold = 0) {
            return Json{{"card_pools", Json::array()}, {"custom_description_encounter_source_id", "NONE.NONE"}, {"gold_amount", gold},
                {"option_count", 0}, {"predetermined_model_id", "NONE.NONE"}, {"rarity_odds", "none"}, {"reward_type", type},
                {"source", "none"}, {"special_card", nullptr}, {"was_gold_stolen_back", false}};
        };
        auto entry = [](const char* kind, Json saved, bool taken) {
            return Json{{"kind", kind}, {"saved", saved}, {"taken", taken}, {"cards", Json::array()}, {"relic", nullptr}, {"potion", nullptr}, {"can_skip", false}};
        };
        Json entries = Json::array({entry("GoldReward", saved_reward("gold", rewards.gold), rewards.gold_taken)});
        if (rewards.potion != no_content) {
            auto p = entry("PotionReward", saved_reward("potion"), rewards.potion_taken);
            p["potion"] = {{"id", SetupRules::name(rewards.potion)}, {"slot_index", -1}}; entries.push_back(std::move(p));
        }
        auto cards = entry("CardReward", nullptr, rewards.card_taken); cards["can_skip"] = true;
        auto saved_card = [](const PersistentCard& c, Json floor = nullptr) {
            return Json{{"current_upgrade_level", c.upgrade}, {"enchantment", nullptr}, {"floor_added_to_deck", floor},
                {"id", SetupRules::name(c.definition)}, {"props", nullptr}};
        };
        for (std::size_t i = 0; i < rewards.cards.size(); ++i) if (i != rewards.selected_card)
            cards["cards"].push_back(saved_card(rewards.cards[i]));
        entries.push_back(std::move(cards));
        Json pending = nullptr, operations = Json::array();
        if (!rewards.resolved) operations.push_back({{"kind", "offer_combat_rewards"}, {"index", nullptr}});
        if (rewards.selecting_card) {
            pending = {{"kind", "card_reward"}, {"cards", entries.back()["cards"]},
                {"alternatives", Json::array({"CardRewardAlternative"})}, {"can_skip", true}};
            operations.push_back({{"kind", "take_reward"}, {"index", rewards.potion == no_content ? 1 : 2}});
        }
        out["run_flow"] = {{"room", {{"kind", "combat_end"}}}, {"pending_choice", pending},
            {"reward_sets", rewards.resolved ? Json::array() : Json::array({{{"rewards", entries}, {"can_skip", true}}})},
            {"operations", operations}, {"combat_rewards_offered", true}};
        stats["gold_gained"] = rewards.gold_taken ? rewards.gold : 0;
        if (rewards.potion_taken || (rewards.skipped && rewards.potion != no_content))
            stats["potion_choices"].push_back({{"choice", SetupRules::name(rewards.potion)}, {"was_picked", rewards.potion_taken}});
        if (rewards.potion_taken) out["players"][0]["discovered_potions"].push_back(SetupRules::name(rewards.potion));
        for (auto potion : run.discarded_potions) stats["potion_discarded"].push_back(SetupRules::name(potion));
        if (rewards.selected_card != UINT8_MAX) {
            const auto& selected = rewards.cards[rewards.selected_card];
            stats["cards_gained"].push_back(saved_card(selected));
            stats["card_choices"].push_back({{"card", saved_card(selected, run.visited.size())}, {"was_picked", true}});
        }
        if (rewards.card_taken || rewards.skipped) for (std::size_t i = 0; i < rewards.cards.size(); ++i)
            if (i != rewards.selected_card) stats["card_choices"].push_back({{"card", saved_card(rewards.cards[i])}, {"was_picked", false}});
    }
    if (run.phase == RunPhase::map) {
        out["current_room"] = map_room; out["run_flow"] = map_flow; out["combat"] = nullptr; out["native_model_state"] = Json::object(); out["pending_choice"] = nullptr;
    }
    if (died) { out["boundary_kind"] = "run_complete"; out["run_ending_reason"] = "death"; }
}
} // namespace sts2::engine
