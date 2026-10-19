#include "state_recording.hpp"
#include "schema.hpp"
#include "schema_validation.hpp"
#include <map>
#include <stdexcept>

namespace sts2::parity {
void validate_state_recording(const nlohmann::json& value, const ActionRecording& recording,
                              const std::set<std::string>& supported_features) {
    static const auto schema = nlohmann::json::parse(states_schema);
    detail::validate(value, schema, schema, "");
    (void)supported_features; // Features describe action/exporter coverage, not run state.
    const auto& actions = recording.json();
    if (value["recording_id"] != actions["recording_id"])
        detail::invalid("/recording_id", "recording identity mismatch");
    if (value["transitions"].size() != actions["actions"].size())
        detail::invalid("/transitions", "expected exactly one state per action");
    // Table labels are document-local, so identity cannot be compared by label
    // across boundaries. Each state validates its own reference graph.
    auto check = [&](const nlohmann::json& state, const std::string& path) {
        try { (void)Snapshot::parse(state); }
        catch (const std::invalid_argument& e) { detail::invalid(path, e.what()); }
    };
    check(value["initial"], "/initial");
    auto boundary = value["initial"]["boundary_kind"];
    for (std::size_t i = 0; i < value["transitions"].size(); ++i) {
        const auto path = "/transitions/" + std::to_string(i);
        if (boundary == "run_complete") detail::invalid(path, "action follows terminal state");
        const auto& step = value["transitions"][i];
        if (step["action_id"] != actions["actions"][i]["id"])
            detail::invalid(path + "/action_id", "action identity/order mismatch");
        check(step["state"], path + "/state");
        boundary = step["state"]["boundary_kind"];
    }
}
}
