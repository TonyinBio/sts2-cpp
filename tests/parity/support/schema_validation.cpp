#include "schema_validation.hpp"
#include "schema.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
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
namespace {
using Evaluated = std::set<std::string>;
// The keyword subset used by the embedded Draft 2020-12 schemas. Return evaluated
// property names so allOf/conditional/$ref annotations reach unevaluatedProperties.
Evaluated check(const Json& value, const Json& rule, const Json& root, const std::string& path) {
    if (rule.is_boolean()) {
        if (!rule.get<bool>()) invalid(path, "disallowed value");
        return {};
    }
    Evaluated evaluated;
    auto merge = [&](const Evaluated& keys) { evaluated.insert(keys.begin(), keys.end()); };
    if (rule.contains("$ref")) {
        const auto ref = rule["$ref"].get<std::string>();
        const auto hash = ref.find('#');
        const auto document = ref.substr(0, hash);
        const Json* target = &root;
        if (!document.empty()) {
            static const auto run = Json::parse(run_parity_schema);
            static const auto native = Json::parse(native_run_schema);
            if (document.ends_with("run-parity-v1.schema.json")) target = &run;
            else if (document.ends_with("serializable-run-v20.schema.json")) target = &native;
            else invalid(path, "unknown schema reference: " + ref);
        }
        const auto fragment = hash == std::string::npos ? "" : ref.substr(hash + 1);
        merge(check(value, target->at(Json::json_pointer(fragment)), *target, path));
    }
    if (rule.contains("allOf"))
        for (const auto& branch : rule["allOf"]) merge(check(value, branch, root, path));
    for (const auto* keyword : {"anyOf", "oneOf"}) {
        if (!rule.contains(keyword)) continue;
        int matches = 0;
        for (const auto& branch : rule[keyword]) {
            try { auto keys = check(value, branch, root, path); ++matches; merge(keys); }
            catch (const std::invalid_argument&) {}
        }
        if (matches == 0 || (std::string(keyword) == "oneOf" && matches != 1))
            invalid(path, std::string("no matching ") + keyword + " shape");
    }
    if (rule.contains("if")) {
        bool matches = true;
        try { (void)check(value, rule["if"], root, path); }
        catch (const std::invalid_argument&) { matches = false; }
        const auto keyword = matches ? "then" : "else";
        if (matches) merge(check(value, rule["if"], root, path));
        if (rule.contains(keyword)) merge(check(value, rule[keyword], root, path));
    }
    if (rule.contains("const") && value != rule["const"]) invalid(path, "unsupported constant");
    if (rule.contains("enum") && std::find(rule["enum"].begin(), rule["enum"].end(), value) == rule["enum"].end())
        invalid(path, "unknown enum value");
    auto matches_type = [&](const std::string& type) {
        return (type == "object" && value.is_object()) || (type == "array" && value.is_array()) ||
            (type == "string" && value.is_string()) || (type == "boolean" && value.is_boolean()) ||
            (type == "null" && value.is_null()) || (type == "number" && value.is_number()) ||
            (type == "integer" && value.is_number_integer());
    };
    if (rule.contains("type")) {
        const auto& type = rule["type"];
        bool matched = false;
        if (type.is_array()) {
            for (const auto& alternative : type) matched = matched || matches_type(alternative.get<std::string>());
        } else matched = matches_type(type.get<std::string>());
        if (!matched) invalid(path, "expected type " + type.dump());
    }
    if (value.is_object()) {
        if (rule.contains("required"))
            for (const auto& key : rule["required"])
                if (!value.contains(key.get<std::string>())) invalid(path + "/" + pointer_token(key.get<std::string>()), "missing required field");
        for (const auto& [key, item] : value.items()) {
            const auto next = path + "/" + pointer_token(key);
            if (rule.contains("propertyNames")) (void)check(key, rule["propertyNames"], root, next);
            if (rule.contains("properties") && rule["properties"].contains(key)) {
                (void)check(item, rule["properties"][key], root, next);
                evaluated.insert(key);
            } else if (rule.contains("additionalProperties")) {
                (void)check(item, rule["additionalProperties"], root, next);
                evaluated.insert(key);
            }
        }
        if (rule.contains("unevaluatedProperties"))
            for (const auto& [key, item] : value.items())
                if (!evaluated.contains(key)) (void)check(item, rule["unevaluatedProperties"], root, path + "/" + pointer_token(key));
    }
    if (value.is_array()) {
        if (value.size() < rule.value("minItems", std::size_t{0})) invalid(path, "too few items");
        if (rule.contains("maxItems") && value.size() > rule["maxItems"].get<std::size_t>()) invalid(path, "too many items");
        std::set<Json> unique;
        for (std::size_t i = 0; i < value.size(); ++i) {
            if (rule.contains("items")) (void)check(value[i], rule["items"], root, path + "/" + std::to_string(i));
            if (rule.value("uniqueItems", false) && !unique.insert(value[i]).second) invalid(path, "duplicate item");
        }
    }
    if (value.is_number()) {
        // Compare unsigned integers without narrowing through int64 or double.
        if (rule.contains("minimum") && value < rule["minimum"]) invalid(path, "number below minimum");
        if (rule.contains("maximum") && value > rule["maximum"]) invalid(path, "number above maximum");
    }
    if (value.is_string()) {
        const auto& text = value.get_ref<const std::string&>();
        if (text.size() < rule.value("minLength", std::size_t{0})) invalid(path, "string too short");
        if (rule.contains("pattern") && !std::regex_search(text, std::regex(rule["pattern"].get<std::string>())))
            invalid(path, "string does not match pattern");
        if (rule.value("format", "") == "uint64-decimal") {
            std::uint64_t word;
            const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), word);
            if (error != std::errc{} || end != text.data() + text.size()) invalid(path, "uint64 overflow");
        }
    }
    return evaluated;
}
}
void validate(const Json& value, const Json& rule, const Json& root, const std::string& path) {
    (void)check(value, rule, root, path);
}
}
