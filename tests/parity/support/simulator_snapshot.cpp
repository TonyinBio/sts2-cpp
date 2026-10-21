#include "simulator_snapshot.hpp"
#include "exact_json.hpp"
#include "sts2/simulator.hpp"
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace sts2::parity {
namespace {
using Json = nlohmann::json;
const Json& at(const Json& value, const std::string& pointer) {
    return value.at(Json::json_pointer(pointer));
}
int integer(const Json& value) {
    if (!value.is_number_integer() || value < 0 || value > std::numeric_limits<int>::max())
        throw std::invalid_argument("Scaffold state requires nonnegative int fields");
    return value.get<int>();
}
}
void SimulatorSnapshot::initialize(const Json& setup) {
    if (setup.at("kind") != "seeded_combat" || setup.at("character") != "Ironclad" ||
        setup.at("ascension") != 0 || setup.at("encounter") != "SHRINKER_BEETLE_WEAK" ||
        !setup.at("seed").is_string() || setup.at("seed").get<std::string>().empty())
        throw std::invalid_argument("Simulator does not implement this run setup");
    SimulatorSnapshot next;
    next.initialized_ = true;
    // These are the actual scaffold defaults. Native seeded initialization is
    // still unsupported and is never filled from expected/reference snapshots.
    for (std::size_t i = 0; i < next.state_.hand_size; ++i) {
        auto id = "card_" + std::to_string(i);
        auto ref = "/combat/cards/" + id;
        next.hand_.push_back(ref); next.card_ids_.emplace(id, ref);
    }
    *this = std::move(next);
}
void SimulatorSnapshot::load(const Snapshot& initial) {
    const auto& value = initial.json();
    if (value["boundary_kind"] != "awaiting_input" || !value["pending_choice"].is_null() ||
        value["combat"].is_null() || value["players"].size() != 1 ||
        value["combat"]["players"].size() != 1 || value["combat"]["enemies"].size() != 1 ||
        !value["combat"]["turn_control"]["is_in_progress"].get<bool>() ||
        value["combat"]["players"][0]["phase"] != "Play")
        throw std::invalid_argument("Scaffold loader requires ongoing single-player, single-enemy Play boundary");
    SimulatorSnapshot next;
    const auto& pc = value["combat"]["players"][0];
    next.player_ = pc["player"].get<std::string>();
    next.creature_ = pc["creature"].get<std::string>();
    next.enemy_ = value["combat"]["enemies"][0].get<std::string>();
    next.state_.player_hp = integer(at(value, next.player_)["current_hp"]);
    next.state_.player_block = integer(at(value, next.creature_)["block"]);
    next.state_.enemy_hp = integer(at(value, next.enemy_)["monster"]["current_hp"]);
    next.state_.energy = integer(pc["energy"]);
    next.state_.turn = integer(pc["turn_number"]);
    if (next.state_.player_hp == 0 || next.state_.enemy_hp == 0)
        throw std::invalid_argument("Scaffold loader requires living combatants");
    next.hand_ = pc["piles"]["Hand"].get<std::vector<std::string>>();
    if (next.hand_.size() > next.state_.hand.size())
        throw std::invalid_argument("Scaffold hand capacity exceeded");
    next.state_.hand_size = static_cast<std::uint8_t>(next.hand_.size());
    for (std::size_t i = 0; i < next.hand_.size(); ++i) {
        const auto id = at(value, next.hand_[i])["saved"]["id"].get<std::string>();
        if (id == "CARD.STRIKE_IRONCLAD") next.state_.hand[i] = Card::attack;
        else if (id == "CARD.DEFEND_IRONCLAD") next.state_.hand[i] = Card::defend;
        else throw std::invalid_argument("Unsupported scaffold hand card: " + id);
    }
    // Match the reference driver's initial enumeration, independent of table labels.
    std::size_t index = 0;
    for (const auto* pile : {"Hand", "Draw", "Discard", "Exhaust", "Play"})
        for (const auto& ref : pc["piles"][pile])
            next.card_ids_.emplace("card_" + std::to_string(index++), ref.get<std::string>());
    next.discarded_ = pc["piles"]["Discard"].get<std::vector<std::string>>();
    next.initialized_ = true;
    *this = std::move(next);
}
Json SimulatorSnapshot::produced_state() const {
    if (!initialized_) throw std::logic_error("Initialize the simulator before exporting");
    Json value = Json::object();
    value[Json::json_pointer(player_ + "/current_hp")] = state_.player_hp;
    value[Json::json_pointer(creature_ + "/block")] = state_.player_block;
    value[Json::json_pointer(enemy_ + "/monster/current_hp")] = state_.enemy_hp;
    value["combat"]["players"] = Json::array({{
        {"energy", state_.energy}, {"turn_number", state_.turn},
        {"piles", {{"Hand", hand_}, {"Discard", discarded_}}}}});
    value["combat"]["turn_control"]["is_in_progress"] = state_.outcome == Outcome::ongoing;
    return value;
}
std::vector<std::string> SimulatorSnapshot::unsupported_fields() const {
    return {"native seeded initialization", "native run fields", "RNG", "room and run progression",
        "card runtime state", "draw/exhaust/play piles", "history", "monster AI", "powers and inventory",
        "pets and orbs", "pending choices", "run endings"};
}
Snapshot SimulatorSnapshot::snapshot() const {
    if (!initialized_) throw std::logic_error("Initialize the simulator before exporting");
    std::string message = "Full run-parity state export is unsupported; missing simulator state: ";
    bool first = true;
    for (const auto& field : unsupported_fields()) {
        if (!first) message += ", "; first = false; message += field;
    }
    throw std::invalid_argument(message);
}
void SimulatorSnapshot::apply(const Json& action) {
    if (!initialized_) throw std::logic_error("Load a snapshot before replaying");
    if (action.at("kind") != "play_card")
        throw std::invalid_argument("Scaffold replay supports play_card only; draw/shuffle identities are not implemented");
    const auto id = action.at("card_instance").get<std::string>();
    const auto found = card_ids_.find(id);
    if (found == card_ids_.end()) throw std::invalid_argument("Unknown card identity: " + id);
    const auto card = std::find(hand_.begin(), hand_.end(), found->second);
    if (card == hand_.end()) throw std::invalid_argument("Card is not in hand: " + id);
    const auto index = static_cast<std::uint8_t>(card - hand_.begin());
    if (state_.hand[index] == Card::attack) {
        if (!action.contains("target") || action["target"] != "enemy_0")
            throw std::invalid_argument("Strike requires target enemy_0");
    } else if (action.contains("target")) {
        throw std::invalid_argument("Defend does not take a target");
    }
    if (!sts2::step(state_, {ActionKind::play_card, index}))
        throw std::invalid_argument("Simulator rejected action for " + id);
    discarded_.push_back(*card);
    hand_.erase(card);
}
ReplayBackend SimulatorSnapshot::backend() {
    return {[this](const Json& setup, const Snapshot&) { initialize(setup); },
            [this](const Json& action) { apply(action); },
            [this] { return snapshot(); }};
}
}
