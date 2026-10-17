#include "snapshot.hpp"
#include "schema.hpp"
#include <algorithm>
#include <charconv>
#include <cstdint>
#include <regex>
#include <stdexcept>

namespace sts2::parity {
namespace {
using Json = nlohmann::json;
[[noreturn]] void invalid(const std::string& path, const std::string& reason) {
    throw std::invalid_argument((path.empty() ? "/" : path) + ": " + reason);
}
std::string pointer_token(const std::string& key) {
    std::string result;
    for (const char c : key) result += c == '~' ? "~0" : c == '/' ? "~1" : std::string(1, c);
    return result;
}
// Implements the keywords used by the embedded combat schema, not a general JSON Schema engine.
void validate(const Json& value, const Json& rule, const Json& root, const std::string& path) {
    if (rule.contains("$ref")) {
        const auto pointer = rule["$ref"].get<std::string>().substr(1);
        validate(value, root.at(Json::json_pointer(pointer)), root, path);
        return;
    }
    if (rule.contains("const") && value != rule["const"]) invalid(path, "unsupported constant");
    if (rule.contains("enum") && std::find(rule["enum"].begin(), rule["enum"].end(), value) == rule["enum"].end())
        invalid(path, "unknown enum value");
    const auto type = rule.value("type", "");
    if (type == "object") {
        if (!value.is_object()) invalid(path, "expected object");
        if (rule.contains("required"))
            for (const auto& key : rule["required"])
                if (!value.contains(key.get<std::string>())) invalid(path + "/" + pointer_token(key.get<std::string>()), "missing required field");
        for (const auto& [key, item] : value.items()) {
            const auto next = path + "/" + pointer_token(key);
            if (rule.contains("properties") && rule["properties"].contains(key))
                validate(item, rule["properties"][key], root, next);
            else if (rule.contains("additionalProperties") && rule["additionalProperties"].is_object())
                validate(item, rule["additionalProperties"], root, next);
            else invalid(next, "unknown field");
        }
    } else if (type == "array") {
        if (!value.is_array()) invalid(path, "expected array");
        if (value.size() < rule.value("minItems", std::size_t{0})) invalid(path, "too few items");
        std::set<Json> unique;
        for (std::size_t i = 0; i < value.size(); ++i) {
            validate(value[i], rule.at("items"), root, path + "/" + std::to_string(i));
            if (rule.value("uniqueItems", false) && !unique.insert(value[i]).second) invalid(path, "duplicate item");
        }
    } else if (type == "integer") {
        if (!value.is_number_integer()) invalid(path, "expected integer");
        if (value.is_number_unsigned() && value.get<std::uint64_t>() > INT64_MAX)
            invalid(path, "integer exceeds signed 64-bit range");
        if (rule.contains("minimum") && value.get<std::int64_t>() < rule["minimum"].get<std::int64_t>())
            invalid(path, "integer below minimum");
        if (rule.contains("maximum") && value.get<std::int64_t>() > rule["maximum"].get<std::int64_t>())
            invalid(path, "integer above maximum");
    } else if (type == "string") {
        if (!value.is_string()) invalid(path, "expected string");
        const auto& text = value.get_ref<const std::string&>();
        if (text.size() < rule.value("minLength", std::size_t{0})) invalid(path, "empty string");
        if (rule.contains("pattern") && !std::regex_match(text, std::regex(rule["pattern"].get<std::string>())))
            invalid(path, "noncanonical decimal string");
        if (rule.value("format", "") == "uint64-decimal") {
            std::uint64_t word;
            const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), word);
            if (error != std::errc{} || end != text.data() + text.size()) invalid(path, "uint64 overflow");
        }
    }
}
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
