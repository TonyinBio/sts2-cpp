#pragma once
#include <nlohmann/json.hpp>
#include <set>
#include <string>
#include <utility>

namespace sts2::parity {
// Ordered player decisions plus reference initialization, without expected state.
class ActionRecording {
public:
    static ActionRecording parse(const nlohmann::json& value,
                                 const std::set<std::string>& supported_features);
    static void validate_entry(const nlohmann::json& value);
    const nlohmann::json& json() const noexcept { return value_; }
private:
    explicit ActionRecording(nlohmann::json value) : value_(std::move(value)) {}
    nlohmann::json value_;
};
} // namespace sts2::parity
