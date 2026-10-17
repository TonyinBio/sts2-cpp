#include "simulator_adapter.hpp"
#include "recording.hpp"
#include "state_recording.hpp"
#include <charconv>
#include <stdexcept>

namespace sts2::parity {
namespace {
using Json = nlohmann::json;
constexpr std::array<const char*, 12> run_names = {"UpFront", "Shuffle", "UnknownMapPoint",
    "CombatCardGeneration", "CombatPotionGeneration", "CombatCardSelection", "CombatEnergyCosts",
    "CombatTargets", "MonsterAi", "Niche", "CombatOrbs", "TreasureRoomRelics"};
constexpr std::array<const char*, 3> player_names = {"Rewards", "Shops", "Transformations"};
constexpr std::array<const char*, 3> move_names = {"SHRINKER_MOVE", "CHOMP_MOVE", "STOMP_MOVE"};
constexpr std::array<const char*, 3> card_names = {"CARD.BASH", "CARD.DEFEND_IRONCLAD", "CARD.STRIKE_IRONCLAD"};
void require(bool condition, const std::string& reason) {
    if (!condition) throw std::invalid_argument(reason);
}
Json features() { return Json::array({"basic_combat", "ironclad_starter_shrinker_v1"}); }
std::uint64_t word(const Json& value) {
    const auto& text = value.get_ref<const std::string&>();
    std::uint64_t result = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), result);
    require(error == std::errc{} && end == text.data() + text.size(), "Invalid RNG word");
    return result;
}
Json rng_json(const Rng& rng) {
    return {{"counter", rng.counter}, {"s0", std::to_string(rng.words[0])},
        {"s1", std::to_string(rng.words[1])}, {"s2", std::to_string(rng.words[2])}, {"s3", std::to_string(rng.words[3])}};
}
Json power_json(const Power& power, bool shrink) {
    Json counters = {{"amount_on_turn_start", power.amount_on_turn_start},
        {"skip_next_duration_tick", power.skip_next_duration_tick ? 1 : 0}, {"applier_actor", shrink ? 1 : 0}};
    if (shrink) counters["damage_decrease_percent"] = 30;
    return {{"instance_id", "power_" + std::to_string(power.instance)},
        {"model_id", shrink ? "POWER.SHRINK_POWER" : "POWER.VULNERABLE_POWER"},
        {"amount", power.amount}, {"counters", counters}};
}
Json pile_json(const Pile& pile, const ImportedCombat& combat) {
    Json result = Json::array();
    for (std::uint8_t i = 0; i < pile.size; ++i) result.push_back(combat.card_ids[pile.cards[i]]);
    return result;
}
std::string mismatch(const Snapshot& expected, const Snapshot& actual) {
    const auto differences = compare(expected, actual);
    if (differences.empty()) return {};
    const auto& d = differences[0];
    return d.path + ": expected " + d.expected.dump() + ", actual " + d.actual.dump();
}
} // namespace

ImportedCombat import_initial(const Snapshot& snapshot) {
    const auto& value = snapshot.json();
    require(value["required_features"] == features(), "Unsupported feature profile");
    require(value["boundary"] == "player_action" && value["combat"]["round"] == 1,
            "Only fresh player-action combat snapshots can be imported");
    require(value["cards"].size() == max_cards, "Expected ten starter cards");
    require(value["enemies"].size() == 1, "Expected one enemy");
    require(value["player"]["max_hp"] == 80 && value["player"]["hp"] == 80, "Expected full-health Ironclad");
    const auto& enemy = value["enemies"][0];
    require(enemy["max_hp"] >= 38 && enemy["max_hp"] <= 40 && enemy["hp"] == enemy["max_hp"],
            "Expected full-health A0 Shrinker Beetle");
    ImportedCombat result;
    auto& state = result.state;
    state.enemy_hp = state.enemy_max_hp = enemy["hp"].get<std::int32_t>();
    std::array<int, 3> counts{};
    for (std::size_t i = 0; i < max_cards; ++i) {
        const auto& card = value["cards"][i];
        result.card_ids[i] = card["instance_id"].get<std::string>();
        const auto model = card["model_id"].get<std::string>();
        std::size_t kind = 0;
        while (kind < card_names.size() && model != card_names[kind]) ++kind;
        require(kind < card_names.size(), "Unsupported card model: " + model);
        state.cards[i] = static_cast<Card>(kind);
        ++counts[kind];
    }
    require(counts == std::array<int, 3>{1, 4, 5}, "Expected unmodified Ironclad starter deck");
    auto pile = [&](const char* name, Pile& destination) {
        require(value["piles"][name].size() <= max_cards, "Pile capacity exceeded");
        for (const auto& id : value["piles"][name]) {
            std::size_t index = 0;
            while (index < max_cards && result.card_ids[index] != id) ++index;
            require(index < max_cards, "Unknown card identity");
            destination.push(static_cast<CardId>(index));
        }
    };
    pile("hand", state.hand); pile("draw", state.draw); pile("discard", state.discard);
    require(state.hand.size == 5 && state.draw.size == 5 && state.discard.size == 0,
            "Expected initial five-card draw");
    auto rng = [&](const Json& input, std::size_t index) {
        auto& r = result.initial_rngs[index];
        r.counter = input["counter"].get<std::uint64_t>();
        require(r.counter <= INT32_MAX, "Reference RNG counter exceeds supported range");
        for (std::size_t i = 0; i < 4; ++i) r.words[i] = word(input["s" + std::to_string(i)]);
        require(r.words != std::array<std::uint64_t, 4>{}, "All-zero xoshiro state is invalid");
    };
    for (std::size_t i = 0; i < run_names.size(); ++i) rng(value["randomness"]["run"][run_names[i]], i);
    for (std::size_t i = 0; i < player_names.size(); ++i) rng(value["randomness"]["player"][player_names[i]], i + run_names.size());
    state.shuffle_rng = result.initial_rngs[static_cast<std::size_t>(RunRng::shuffle)];
    // Exact round-trip rejects anything not explicitly represented (powers, counters,
    // identities, upgrades, flags, costs, AI state, nonempty extra piles, etc.).
    const auto difference = mismatch(snapshot, export_snapshot(result));
    require(difference.empty(), "Unsupported initial state: " + difference);
    return result;
}

Snapshot export_snapshot(const ImportedCombat& imported) {
    const auto& s = imported.state;
    Json player_powers = Json::array(), enemy_powers = Json::array();
    if (s.shrink.amount) player_powers.push_back(power_json(s.shrink, true));
    if (s.vulnerable.amount) enemy_powers.push_back(power_json(s.vulnerable, false));
    Json cards = Json::array();
    for (std::size_t i = 0; i < s.card_count; ++i) {
        const auto kind = s.cards[i];
        cards.push_back({{"instance_id", imported.card_ids[i]}, {"model_id", card_names[static_cast<std::size_t>(kind)]},
            {"upgrade_level", 0}, {"cost", {{"kind", "fixed"}, {"amount", card_cost(kind)}}},
            {"flags", Json::array()}, {"counters", {{"current_play_index", 0}}}});
    }
    Json history = Json::array();
    for (std::uint32_t i = 0; i < s.moves_performed; ++i)
        history.push_back(move_names[i == 0 ? 0 : (i % 2 ? 1 : 2)]);
    Json machine = {{"performed_first_move", s.moves_performed ? 1 : 0}};
    for (std::size_t i = 0; i < move_names.size(); ++i)
        machine[std::string("performed_move:") + move_names[i]] =
            s.current_move_performed && static_cast<std::size_t>(s.enemy_move) == i ? 1 : 0;
    Json randomness = {{"run", Json::object()}, {"player", Json::object()}};
    for (std::size_t i = 0; i < run_names.size(); ++i) randomness["run"][run_names[i]] = rng_json(i == static_cast<std::size_t>(RunRng::shuffle) ? s.shuffle_rng : imported.initial_rngs[i]);
    for (std::size_t i = 0; i < player_names.size(); ++i) randomness["player"][player_names[i]] = rng_json(imported.initial_rngs[i + run_names.size()]);
    const bool ended = s.outcome != Outcome::ongoing;
    Json value = {
        {"schema_version", 1}, {"profile", "combat-v1"}, {"required_features", features()},
        {"boundary", ended ? "combat_end" : "player_action"},
        {"combat", {{"round", s.turn}, {"active_side", ended ? "none" : "player"},
            {"outcome", s.outcome == Outcome::ongoing ? "ongoing" : s.outcome == Outcome::victory ? "victory" : "defeat"}}},
        {"player", {{"instance_id", "player_0"}, {"model_id", "CHARACTER.IRONCLAD"},
            {"hp", s.player_hp}, {"max_hp", s.player_max_hp}, {"block", s.player_block},
            {"energy", s.energy}, {"max_energy", s.max_energy}, {"powers", player_powers},
            {"counters", {{"turn_number", s.turn}}}, {"relics", Json::array({
                {{"instance_id", "relic_0"}, {"model_id", "RELIC.BURNING_BLOOD"}, {"counters", Json::object()}}})}}},
        {"enemies", Json::array({{{"instance_id", "enemy_0"}, {"model_id", "MONSTER.SHRINKER_BEETLE"},
            {"hp", s.enemy_hp}, {"max_hp", s.enemy_max_hp}, {"block", s.enemy_block}, {"powers", enemy_powers},
            {"counters", machine}, {"move_id", move_names[static_cast<std::size_t>(s.enemy_move)]}, {"move_history", history}}})},
        {"cards", cards}, {"piles", {{"hand", pile_json(s.hand, imported)}, {"draw", pile_json(s.draw, imported)},
            {"discard", pile_json(s.discard, imported)}, {"exhaust", Json::array()}, {"playing", Json::array()}}},
        {"selection", {{"kind", "none"}, {"options", Json::array()}, {"min", 0}, {"max", 0}}}, {"randomness", randomness},
    };
    return Snapshot::parse(value, simulator_features);
}

Action resolve_action(const ImportedCombat& imported, const Json& action) {
    const auto kind = action["kind"].get<std::string>();
    if (kind == "end_turn") return {ActionKind::end_turn};
    require(kind == "play_card", "Unsupported simulator action: " + kind);
    const auto id = action["card_instance"].get<std::string>();
    std::uint8_t index = 0;
    while (index < imported.state.hand.size && imported.card_ids[imported.state.hand.cards[index]] != id) ++index;
    require(index < imported.state.hand.size, "Card not in hand: " + id);
    const bool target = action.contains("target");
    require(!target || action["target"] == "enemy_0", "Unsupported target identity");
    return {ActionKind::play_card, index, static_cast<std::int8_t>(target ? 0 : -1)};
}

void replay_and_compare(const Json& actions, const Json& reference) {
    const auto recording = ActionRecording::parse(actions, simulator_features);
    validate_state_recording(reference, recording, simulator_features);
    require(reference["reference"]["revision"] == "084d1aa3d8e118ca7ce8d8774ad16d6be9c92367",
        "Unsupported sts2-cli revision");
    require(reference["reference"]["game_dll_sha256"] ==
        "0861bfa1df347538d932f22d580e75420f08082792eb914e53b4882764acdbe9", "Unsupported reference game build");
    require(actions["setup"]["character"] == "Ironclad" && actions["setup"]["ascension"] == 0 &&
        actions["setup"]["encounter"] == "SHRINKER_BEETLE_WEAK", "Unsupported reference setup");
    auto combat = import_initial(Snapshot::parse(reference["initial"], simulator_features));
    for (std::size_t i = 0; i < actions["actions"].size(); ++i) {
        const auto label = actions["recording_id"].get<std::string>() + ": action " + std::to_string(i) +
            " (" + actions["actions"][i]["id"].get<std::string>() + ")";
        try {
            const auto action = resolve_action(combat, actions["actions"][i]["action"]);
            require(step(combat.state, action), "Action rejected by simulator");
            const auto expected = Snapshot::parse(reference["transitions"][i]["state"], simulator_features);
            const auto difference = mismatch(expected, export_snapshot(combat));
            require(difference.empty(), difference);
        } catch (const std::exception& e) { throw std::runtime_error(label + ": " + e.what()); }
    }
}
} // namespace sts2::parity
