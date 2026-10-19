#pragma once
#include <nlohmann/json.hpp>
#include <map>
#include <set>
#include <string_view>
#include <string>
#include <vector>
#include <utility>

namespace sts2::parity {
// Test-only canonical state. Construction requires schema and semantic validation.
class Snapshot {
public:
    static Snapshot parse(const nlohmann::json& value);
    // Use for wire/file input: retains exact JSON decimal tokens for comparison.
    static Snapshot parse_text(std::string_view text);
    const std::map<std::string, std::string>& numbers() const noexcept { return numbers_; }
    const std::set<std::string>& references() const noexcept { return references_; }
    const nlohmann::json& json() const noexcept { return value_; }
private:
    explicit Snapshot(nlohmann::json value) : value_(std::move(value)) {}
    nlohmann::json value_;
    std::map<std::string, std::string> numbers_;
    std::set<std::string> references_;
};
struct Difference {
    std::string path; // JSON Pointer, suitable for identifying a fixture field.
    nlohmann::json expected;
    nlohmann::json actual;
};
// Exact comparison; no numeric tolerances or ignored fields.
std::vector<Difference> compare(const Snapshot& expected, const Snapshot& actual);
} // namespace sts2::parity
