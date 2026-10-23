#include "sts2/parity/recording.hpp"
#include "schema.hpp"
#include "schema_validation.hpp"

namespace sts2::parity {
void ActionRecording::validate_entry(const nlohmann::json& value) {
    static const auto schema = nlohmann::json::parse(actions_schema);
    detail::validate(value, schema["properties"]["actions"]["items"], schema, "");
}

ActionRecording ActionRecording::parse(const nlohmann::json& value,
                                      const std::set<std::string>& supported_features) {
    static const auto schema = nlohmann::json::parse(actions_schema);
    detail::validate(value, schema, schema, "");
    for (const auto& feature : value["required_features"])
        if (!supported_features.contains(feature.get<std::string>()))
            detail::invalid("/required_features", "unsupported feature: " + feature.get<std::string>());
    std::set<std::string> ids;
    for (std::size_t i = 0; i < value["actions"].size(); ++i) {
        const auto& entry = value["actions"][i];
        if (!ids.insert(entry["id"].get<std::string>()).second)
            detail::invalid("/actions/" + std::to_string(i) + "/id", "duplicate action ID");
    }
    return ActionRecording(value);
}
} // namespace sts2::parity
