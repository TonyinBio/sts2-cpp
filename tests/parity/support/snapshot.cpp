#include "snapshot.hpp"
#include "exact_json.hpp"
#include "schema.hpp"
#include "schema_validation.hpp"
#include <algorithm>
#include <set>
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
                 std::vector<Difference>& result,
                 const std::map<std::string, std::string>& expected_numbers,
                 const std::map<std::string, std::string>& actual_numbers) {
    if (expected.is_object() && actual.is_object()) {
        for (const auto& [key, value] : expected.items()) {
            const auto next = path + "/" + pointer_token(key);
            if (!actual.contains(key)) result.push_back({next, value, nullptr});
            else differences(value, actual.at(key), next, result, expected_numbers, actual_numbers);
        }
        for (const auto& [key, value] : actual.items())
            if (!expected.contains(key)) result.push_back({path + "/" + pointer_token(key), nullptr, value});
    } else if (expected.is_array() && actual.is_array() && expected.size() == actual.size()) {
        for (std::size_t i = 0; i < expected.size(); ++i)
            differences(expected[i], actual[i], path + "/" + std::to_string(i), result, expected_numbers, actual_numbers);
    } else if (expected.is_number() && actual.is_number()) {
        if (expected_numbers.at(path) != actual_numbers.at(path))
            result.push_back({path, expected == actual ? Json(expected_numbers.at(path)) : expected,
                                   expected == actual ? Json(actual_numbers.at(path)) : actual});
    } else if (expected != actual) result.push_back({path, expected, actual});
}
// Object table keys are local labels. Build a bijection by following ordered
// gameplay roots and then retained objects; references preserve sharing/cycles.
const std::set<std::string> tables = {"creatures", "cards", "powers", "orbs", "afflictions",
    "card_plays", "attacks", "retained_inventory_models"};
bool table_path(const std::string& path) {
    return path.starts_with("/combat/") && tables.contains(path.substr(8));
}
std::string object_ref(const std::string& ref) {
    if (!ref.starts_with("/combat/")) return "";
    const auto slash = ref.find('/', 8);
    if (slash == std::string::npos || !tables.contains(ref.substr(8, slash - 8))) return "";
    const auto end = ref.find('/', slash + 1);
    return ref.substr(0, end);
}
void unordered_collections(Json& value, const std::string& path = "",
                           std::map<std::string, std::string>* numbers = nullptr) {
    if (value.is_object()) for (auto& [key, item] : value.items()) unordered_collections(item, path + "/" + pointer_token(key), numbers);
    if (!value.is_array()) return;
    for (std::size_t i = 0; i < value.size(); ++i) unordered_collections(value[i], path + "/" + std::to_string(i), numbers);
    const auto field = path.substr(path.rfind('/') + 1);
    static const std::set<std::string> sets = {"local_keywords", "spawned_enemy_ids",
        "players_ready_to_end_turn", "players_ready_to_begin_enemy_turn"};
    // ModelFields explicitly encode sets and reference-keyed dictionaries.
    const bool model_set = path.find("/model_fields/") != std::string::npos &&
        (field == "Hoarder._cardsToSkip" || field == "BingBong._cardsToSkip" || field == "DampenPower.casters" || field == "DampenPower.downgradedCardsToOldUpgradeLevels" ||
         (!value.empty() && value[0].is_object() && value[0].contains("key") && value[0].contains("value")));
    if (sets.contains(field) || model_set) {
        std::vector<std::size_t> order(value.size());
        for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
        std::stable_sort(order.begin(), order.end(), [&](auto a, auto b) { return value[a] < value[b]; });
        Json sorted = Json::array();
        auto original = numbers ? *numbers : std::map<std::string, std::string>{};
        for (std::size_t i = 0; i < order.size(); ++i) {
            sorted.push_back(value[order[i]]);
            if (numbers) {
                const auto old_prefix = path + "/" + std::to_string(order[i]);
                const auto new_prefix = path + "/" + std::to_string(i);
                for (const auto& [key, number] : original)
                    if (key == old_prefix || key.starts_with(old_prefix + "/"))
                        (*numbers)[new_prefix + key.substr(old_prefix.size())] = number;
            }
        }
        value = std::move(sorted);
    }
}
struct Aligned {
    Json value;
    std::map<std::string, std::string> numbers;
};
Aligned align(const Snapshot& expected, const Snapshot& actual) {
    Json left = expected.json(), right = actual.json();
    auto right_numbers = actual.numbers();
    unordered_collections(left); unordered_collections(right, "", &right_numbers);
    std::map<std::string, std::string> forward, reverse;
    auto walk = [&](auto&& self, const Json& a, const Json& b, const std::string& path) -> void {
        if (table_path(path)) return;
        if (a.is_string() && b.is_string() && expected.references().contains(path)) {
            const auto ar = object_ref(a.get<std::string>()), br = object_ref(b.get<std::string>());
            if (ar.empty() || br.empty() || ar.substr(0, ar.rfind('/')) != br.substr(0, br.rfind('/'))) return;
            if (forward.contains(ar) || reverse.contains(br)) return;
            forward[ar] = br; reverse[br] = ar;
            self(self, left.at(Json::json_pointer(ar)), right.at(Json::json_pointer(br)), ar);
        } else if (a.is_object() && b.is_object()) {
            for (const auto& [key, item] : a.items()) if (b.contains(key)) self(self, item, b[key], path + "/" + pointer_token(key));
        } else if (a.is_array() && b.is_array()) {
            const auto field = path.substr(path.rfind('/') + 1);
            const bool reference_collection = path.find("/model_fields/") != std::string::npos &&
                (field == "Hoarder._cardsToSkip" || field == "BingBong._cardsToSkip" || field == "DampenPower.casters" ||
                 (!a.empty() && a[0].is_object() && a[0].contains("key") && a[0].contains("value")));
            auto shape = [&](auto&& visit, const Json& node, const Json& document, int depth) -> Json {
                if (node.is_string()) {
                    const auto ref = object_ref(node.get<std::string>());
                    if (!ref.empty()) return depth > 0 ? visit(visit, document.at(Json::json_pointer(ref)), document, depth - 1)
                                                       : Json(ref.substr(0, ref.rfind('/')));
                }
                if (node.is_object() || node.is_array()) {
                    auto result = node;
                    for (auto& item : result) item = visit(visit, item, document, depth);
                    return result;
                }
                return node;
            };
            std::set<std::size_t> used;
            for (std::size_t i = 0; i < std::min(a.size(), b.size()); ++i) {
                std::size_t match = i;
                if (reference_collection) {
                    const auto signature = shape(shape, a[i], left, 2);
                    for (std::size_t j = 0; j < b.size(); ++j)
                        if (!used.contains(j) && signature == shape(shape, b[j], right, 2)) { match = j; break; }
                    if (used.contains(match)) for (std::size_t j = 0; j < b.size(); ++j)
                        if (!used.contains(j)) { match = j; break; }
                }
                used.insert(match);
                self(self, a[i], b[match], path + "/" + std::to_string(i));
            }
        }
    };
    walk(walk, left, right, "");
    if (!left["combat"].is_null() && !right["combat"].is_null())
        for (const auto& table : tables) {
            const auto prefix = "/combat/" + table + "/";
            for (const auto& [key, item] : left["combat"][table].items()) {
                const auto ar = prefix + key;
                if (forward.contains(ar)) continue;
                for (const auto& [other, candidate] : right["combat"][table].items()) {
                    const auto br = prefix + other;
                    if (reverse.contains(br)) continue;
                    // Prefer the same retained model; labels themselves are immaterial.
                    auto model = [](const Json& obj) -> Json {
                        if (obj.contains("saved")) return obj["saved"].value("id", Json());
                        if (obj.contains("id")) return obj["id"];
                        return nullptr;
                    };
                    if (model(item) != model(candidate)) continue;
                    forward[ar] = br; reverse[br] = ar;
                    walk(walk, item, candidate, ar);
                    break;
                }
            }
        }
    if (!right["combat"].is_null()) for (const auto& table : tables) {
        std::size_t extra = 0;
        const auto prefix = "/combat/" + table + "/";
        for (const auto& [key, item] : right["combat"][table].items()) {
            (void)item;
            if (reverse.contains(prefix + key)) continue;
            std::string name;
            do { name = "__unmatched_" + std::to_string(extra++); }
            while (!left["combat"].is_null() && left["combat"][table].contains(name));
            reverse[prefix + key] = prefix + name;
        }
    }
    auto rename = [&](const std::string& path) {
        const auto base = object_ref(path);
        return reverse.contains(base) ? reverse.at(base) + path.substr(base.size()) : path;
    };
    auto rewrite = [&](auto&& self, Json& node, const std::string& path) -> void {
        if (node.is_string() && actual.references().contains(path)) {
            const auto ref = node.get<std::string>();
            if (!object_ref(ref).empty()) node = rename(ref);
        } else if (node.is_array()) {
            for (std::size_t i = 0; i < node.size(); ++i) self(self, node[i], path + "/" + std::to_string(i));
        } else if (node.is_object()) {
            Json result = Json::object();
            for (auto& [key, item] : node.items()) {
                self(self, item, path + "/" + pointer_token(key));
                const auto mapped = table_path(path) ? rename(path + "/" + key).substr(path.size() + 1) : key;
                result[mapped] = std::move(item);
            }
            node = std::move(result);
        }
    };
    rewrite(rewrite, right, "");
    std::map<std::string, std::string> numbers;
    for (const auto& [path, number] : right_numbers) numbers[rename(path)] = number;
    unordered_collections(right, "", &numbers);
    return {std::move(right), std::move(numbers)};
}
} // namespace

Snapshot Snapshot::parse(const Json& value) {
    static const auto schema = Json::parse(run_parity_schema);
    validate(value, schema, schema, "");
    auto resolve = [&](const std::string& ref, const std::string& path) -> const Json& {
        try {
            const auto& target = value.at(Json::json_pointer(ref));
            if (target.is_null()) invalid(path, "reference resolves to null");
            return target;
        } catch (const Json::exception&) { invalid(path, "unresolved reference: " + ref); }
    };
    std::set<std::string> references;
    // Only Ref definitions are pointers; native strings, IDs and model fields can
    // legitimately contain slashes. Walk the contract to distinguish them.
    auto refs = [&](auto&& self, const Json& node, const Json& rule, const std::string& path) -> void {
        if (rule.contains("$ref")) {
            const auto ref = rule["$ref"].get<std::string>();
            if (ref == "#/$defs/Ref") { (void)resolve(node.get<std::string>(), path); references.insert(path); return; }
            if (ref.starts_with("#")) self(self, node, schema.at(Json::json_pointer(ref.substr(1))), path);
            return;
        }
        for (const auto* keyword : {"anyOf", "oneOf", "allOf"})
            if (rule.contains(keyword)) for (const auto& branch : rule[keyword]) {
                try { validate(node, branch, schema, path); }
                catch (const std::invalid_argument&) { continue; }
                self(self, node, branch, path);
            }
        if (node.is_object()) for (const auto& [key, item] : node.items()) {
            if (rule.contains("properties") && rule["properties"].contains(key))
                self(self, item, rule["properties"][key], path + "/" + pointer_token(key));
            else if (rule.contains("additionalProperties") && rule["additionalProperties"].is_object())
                self(self, item, rule["additionalProperties"], path + "/" + pointer_token(key));
        }
        if (node.is_array() && rule.contains("items"))
            for (std::size_t i = 0; i < node.size(); ++i) self(self, node[i], rule["items"], path + "/" + std::to_string(i));
    };
    refs(refs, value, schema, "");
    for (const auto& [ref, state] : value["native_model_state"].items()) {
        (void)state;
        if (ref.starts_with("/combat/") || ref.starts_with("/native_model_state/"))
            invalid("/native_model_state", "expected reference into native run data");
        if (!resolve(ref, "/native_model_state").is_object()) invalid("/native_model_state", "expected native model object");
    }
    const auto& combat = value["combat"];
    if (!combat.is_null()) {
        auto target = [&](const Json& ref, const std::string& prefix, const std::string& path) -> const Json& {
            const auto name = ref.get<std::string>();
            if (!name.starts_with(prefix)) invalid(path, "incorrect reference target kind");
            return resolve(name, path);
        };
        std::set<std::string> placed, owners, creatures, powers;
        for (const auto& pc : combat["players"]) {
            const auto& player = target(pc["player"], "/players/", "/combat/players/player");
            if (!player.contains("deck")) invalid("/combat/players/player", "expected native player");
            if (!owners.insert(pc["player"].get<std::string>()).second) invalid("/combat/players", "duplicate player");
            const auto& creature = target(pc["creature"], "/combat/creatures/", "/combat/players/creature");
            if (creature["player"] != pc["player"]) invalid("/combat/players", "creature/player ownership mismatch");
            for (const auto& pile : pc["piles"]) for (const auto& ref : pile) {
                const auto& card = target(ref, "/combat/cards/", "/combat/players/piles");
                if (card["owner"] != pc["player"]) invalid("/combat/players/piles", "card owner mismatch");
                if (!placed.insert(ref.get<std::string>()).second) invalid("/combat/players/piles", "card appears in multiple piles");
            }
            for (const auto& ref : pc["pets"]) (void)target(ref, "/combat/creatures/", "/combat/players/pets");
            for (const auto& ref : pc["orbs"]) (void)target(ref, "/combat/orbs/", "/combat/players/orbs");
        }
        for (const auto* collection : {"allies", "enemies", "escaped_creatures"})
            for (const auto& ref : combat[collection]) {
                (void)target(ref, "/combat/creatures/", std::string("/combat/") + collection);
                if (!creatures.insert(ref.get<std::string>()).second) invalid("/combat", "creature in multiple collections");
            }
        for (const auto& creature : combat["creatures"]) {
            if (!creature["player"].is_null()) (void)target(creature["player"], "/players/", "/combat/creatures/player");
            for (const auto& ref : creature["powers"]) {
                (void)target(ref, "/combat/powers/", "/combat/creatures/powers");
                if (!powers.insert(ref.get<std::string>()).second) invalid("/combat/creatures/powers", "power has multiple owners");
            }
        }
        for (const auto& card : combat["cards"]) {
            if (!card["owner"].is_null()) (void)target(card["owner"], "/players/", "/combat/cards/owner");
            if (!card["deck_version"].is_null()) {
                const auto ref = card["deck_version"].get<std::string>();
                const auto prefix = card["owner"].is_string() ? card["owner"].get<std::string>() + "/deck/" : "";
                if (prefix.empty() || !ref.starts_with(prefix) || !resolve(ref, "/combat/cards/deck_version").contains("id"))
                    invalid("/combat/cards/deck_version", "expected owner's native deck card");
            }
            if (!card["clone_of"].is_null()) (void)target(card["clone_of"], "/combat/cards/", "/combat/cards/clone_of");
            if (!card["affliction"].is_null()) (void)target(card["affliction"], "/combat/afflictions/", "/combat/cards/affliction");
        }
    }
    const auto& choice = value["pending_choice"];
    if (!choice.is_null()) {
        if (choice["min_select"] > choice["max_select"] || choice["max_select"].get<std::size_t>() > choice["candidates"].size())
            invalid("/pending_choice", "inconsistent selection bounds");
        if (!choice["player"].get<std::string>().starts_with("/players/")) invalid("/pending_choice/player", "expected native player");
        std::set<std::string> candidates;
        for (const auto& ref : choice["candidates"]) {
            const auto name = ref.get<std::string>();
            if (!name.starts_with("/combat/cards/") || !candidates.insert(name).second)
                invalid("/pending_choice/candidates", "expected distinct combat cards");
        }
    }
    auto result = Snapshot(value);
    result.numbers_ = detail::ExactDocument::parse(value.dump()).numbers;
    result.references_ = std::move(references);
    return result;
}
Snapshot Snapshot::parse_text(std::string_view text) {
    auto document = detail::ExactDocument::parse(text);
    auto result = parse(document.value);
    result.numbers_ = std::move(document.numbers);
    return result;
}
std::vector<Difference> compare(const Snapshot& expected, const Snapshot& actual) {
    std::vector<Difference> result;
    auto aligned = align(expected, actual);
    auto normalized_expected = expected.json();
    auto expected_numbers = expected.numbers();
    unordered_collections(normalized_expected, "", &expected_numbers);
    differences(normalized_expected, aligned.value, "", result, expected_numbers, aligned.numbers);
    return result;
}
} // namespace sts2::parity
