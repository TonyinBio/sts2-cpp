#include "schema_validation.hpp"
#include "schema.hpp"
#include <algorithm>
#include <charconv>
#include <cstdint>
#include <regex>
#include <set>
#include <stdexcept>
namespace sts2::parity::detail {
using Json = nlohmann::json;
[[noreturn]] void invalid(const std::string& path, const std::string& reason) {
    throw std::invalid_argument((path.empty() ? "/" : path) + ": " + reason);
}
std::string pointer_token(const std::string& key) {
    std::string result;
    for (const char c : key) result += c == '~' ? "~0" : c == '/' ? "~1" : std::string(1, c);
    return result;
}
// Implements only the keywords used by our embedded schemas, not general JSON Schema.
void validate(const Json& value, const Json& rule, const Json& root, const std::string& path) {
    if (rule.contains("oneOf")) {
        int matches = 0;
        for (const auto& alternative : rule["oneOf"]) {
            try { validate(value, alternative, root, path); ++matches; }
            catch (const std::invalid_argument&) {}
        }
        if (matches != 1) invalid(path, "expected exactly one supported action shape");
    }
    if (rule.contains("$ref")) {
        if (rule["$ref"] == "combat-v1.json") {
            static const auto combat = Json::parse(combat_schema);
            validate(value, combat, combat, path);
            return;
        }
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
}
