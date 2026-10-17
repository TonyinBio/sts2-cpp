#pragma once
#include <nlohmann/json.hpp>
#include <set>
#include <string>
#include <vector>
#include <utility>

namespace sts2::parity {
// Test-only canonical state. Construction requires schema and semantic validation.
class Snapshot {
public:
    static Snapshot parse(const nlohmann::json& value,
                          const std::set<std::string>& supported_features);
    const nlohmann::json& json() const noexcept { return value_; }
private:
    explicit Snapshot(nlohmann::json value) : value_(std::move(value)) {}
    nlohmann::json value_;
};
struct Difference {
    std::string path; // JSON Pointer, suitable for identifying a fixture field.
    nlohmann::json expected;
    nlohmann::json actual;
};
// Exact comparison; no numeric tolerances or ignored fields.
std::vector<Difference> compare(const Snapshot& expected, const Snapshot& actual);
} // namespace sts2::parity
