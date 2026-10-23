#pragma once
#include <nlohmann/json.hpp>
#include <map>
#include <string>
#include <string_view>
#include <vector>
#include <stdexcept>
#include "schema_validation.hpp"

namespace sts2::parity::detail {
// Canonical decimal coefficient/exponent, with no binary floating point step.
inline std::string decimal_key(std::string token) {
    bool negative = token.front() == '-';
    if (negative) token.erase(0, 1);
    auto e = token.find_first_of("eE");
    long long exponent = e == std::string::npos ? 0 : std::stoll(token.substr(e + 1));
    token = token.substr(0, e);
    auto dot = token.find('.');
    if (dot != std::string::npos) { exponent -= static_cast<long long>(token.size() - dot - 1); token.erase(dot, 1); }
    auto first = token.find_first_not_of('0');
    if (first == std::string::npos) return "0";
    token.erase(0, first);
    while (token.back() == '0') { token.pop_back(); ++exponent; }
    return (negative ? "-" : "") + token + "e" + std::to_string(exponent);
}
struct ExactDocument : nlohmann::json_sax<nlohmann::json> {
    using Json = nlohmann::json;
    struct Frame { Json value; std::string path, key; };
    std::vector<Frame> frames;
    Json value;
    std::map<std::string, std::string> numbers;
    std::string path() const {
        if (frames.empty()) return "";
        const auto& f = frames.back();
        return f.path + "/" + (f.value.is_array() ? std::to_string(f.value.size()) : pointer_token(f.key));
    }
    bool put(Json item) {
        if (frames.empty()) value = std::move(item);
        else if (frames.back().value.is_array()) frames.back().value.push_back(std::move(item));
        else frames.back().value[frames.back().key] = std::move(item);
        return true;
    }
    bool null() override { return put(nullptr); }
    bool boolean(bool v) override { return put(v); }
    bool number_integer(number_integer_t v) override { numbers[path()] = decimal_key(std::to_string(v)); return put(v); }
    bool number_unsigned(number_unsigned_t v) override { numbers[path()] = decimal_key(std::to_string(v)); return put(v); }
    bool number_float(number_float_t v, const string_t& token) override { numbers[path()] = decimal_key(token); return put(v); }
    bool string(string_t& v) override { return put(v); }
    bool binary(binary_t&) override { throw std::invalid_argument("binary JSON is unsupported"); }
    bool start_object(std::size_t) override { frames.push_back({Json::object(), path(), ""}); return true; }
    bool start_array(std::size_t) override { frames.push_back({Json::array(), path(), ""}); return true; }
    bool key(string_t& key) override {
        if (frames.back().value.contains(key)) invalid(path(), "duplicate JSON key: " + key);
        frames.back().key = key; return true;
    }
    bool finish() { auto item = std::move(frames.back().value); frames.pop_back(); return put(std::move(item)); }
    bool end_object() override { return finish(); }
    bool end_array() override { return finish(); }
    bool parse_error(std::size_t, const std::string&, const nlohmann::detail::exception& error) override {
        throw std::invalid_argument(error.what());
    }
    static ExactDocument parse(std::string_view text) {
        ExactDocument result;
        Json::sax_parse(text, &result);
        return result;
    }
};
}
