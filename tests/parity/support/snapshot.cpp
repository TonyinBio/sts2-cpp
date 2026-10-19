#include "snapshot.hpp"
#include "schema.hpp"
#include "schema_validation.hpp"
#include <algorithm>
#include <charconv>
#include <cstdint>
#include <regex>
#include <stdexcept>

namespace sts2::parity {
namespace {
using Json = nlohmann::json;
using detail::invalid;
using detail::pointer_token;
using detail::validate;
void differences(const Json& expected, const Json& actual, const std::string& path,
                 std::vector<Difference>& result) {
    if (expected == actual) return;
    if (expected.is_object() && actual.is_object()) {
        for (const auto& [key, value] : expected.items()) {
            const auto next = path + "/" + pointer_token(key);
            if (!actual.contains(key)) result.push_back({next, value, nullptr});
            else differences(value, actual.at(key), next, result);
        }
        for (const auto& [key, value] : actual.items())
            if (!expected.contains(key)) result.push_back({path + "/" + pointer_token(key), nullptr, value});
    } else if (expected.is_array() && actual.is_array() && expected.size() == actual.size()) {
        for (std::size_t i = 0; i < expected.size(); ++i)
            differences(expected[i], actual[i], path + "/" + std::to_string(i), result);
    } else result.push_back({path, expected, actual});
}
} // namespace

Snapshot Snapshot::parse(const Json& value, const std::set<std::string>& supported_features) {
    static const auto schema = Json::parse(combat_schema);
    validate(value, schema, schema, "");
    for (const auto& feature : value["required_features"])
        if (!supported_features.contains(feature.get<std::string>()))
            invalid("/required_features", "unsupported feature: " + feature.get<std::string>());
    const auto& combat = value["combat"];
    const bool ended = combat["outcome"] != "ongoing";
    if (ended != (value["boundary"] == "combat_end") ||
        (ended && combat["active_side"] != "none") ||
        (!ended && combat["active_side"] == "none") ||
        (value["boundary"] == "player_action" && combat["active_side"] != "player"))
        invalid("/combat", "inconsistent decision boundary");
    const auto& selection = value["selection"];
    const bool selecting = value["boundary"] == "selection";
    if (selecting != (selection["kind"] != "none") || selection["min"] > selection["max"] ||
        selection["max"].get<std::size_t>() > selection["options"].size() ||
        (!selecting && (!selection["options"].empty() || selection["min"] != 0 || selection["max"] != 0)))
        invalid("/selection", "inconsistent selection");
    std::set<std::string> identities;
    std::set<std::string> actors;
    auto identity = [&](const Json& object) {
        if (!identities.insert(object["instance_id"].get<std::string>()).second)
            invalid("/", "duplicate instance identity");
    };
    auto actor = [&](const Json& object) {
        identity(object);
        actors.insert(object["instance_id"].get<std::string>());
        if (object["hp"] > object["max_hp"]) invalid("/", "hp exceeds max_hp");
        for (const auto& power : object["powers"]) identity(power);
    };
    actor(value["player"]);
    for (const auto& relic : value["player"]["relics"]) identity(relic);
    for (const auto& enemy : value["enemies"]) actor(enemy);
    std::set<std::string> cards;
    for (const auto& card : value["cards"]) {
        identity(card);
        cards.insert(card["instance_id"].get<std::string>());
        if (card["cost"]["kind"] != "fixed" && card["cost"]["amount"] != 0)
            invalid("/cards", "non-fixed cost must have zero amount");
    }
    std::set<std::string> placed;
    for (const auto& pile : value["piles"])
        for (const auto& id : pile) {
            const auto name = id.get<std::string>();
            if (!cards.contains(name)) invalid("/piles", "unknown card: " + name);
            if (!placed.insert(name).second) invalid("/piles", "card in multiple piles: " + name);
        }
    if (cards != placed) invalid("/piles", "every live card must belong to exactly one pile");
    for (const auto& id : selection["options"]) {
        const auto name = id.get<std::string>();
        if (selection["kind"] == "card" ? !cards.contains(name) : !actors.contains(name))
            invalid("/selection/options", "unknown selection identity: " + name);
    }
    return Snapshot(value);
}
std::vector<Difference> compare(const Snapshot& expected, const Snapshot& actual) {
    std::vector<Difference> result;
    differences(expected.json(), actual.json(), "", result);
    return result;
}
} // namespace sts2::parity
