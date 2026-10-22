#include "state_recording.hpp"
#include "schema.hpp"
#include "schema_validation.hpp"
#include <map>
#include <stdexcept>

namespace sts2::parity {
void validate_state_recording(const nlohmann::json& value, const ActionRecording& recording,
                              const std::set<std::string>& supported_features,
                              const std::function<void(const nlohmann::json&)>& validate_snapshot) {
    // Validate the envelope here and each snapshot below. The original schema
    // recursively checked all snapshots before Snapshot::parse checked them again.
    static const auto schema = [] {
        auto result = nlohmann::json::parse(states_schema);
        result["properties"]["initial"] = true;
        result["properties"]["transitions"]["items"]["properties"]["state"] = true;
        return result;
    }();
    detail::validate(value, schema, schema, "");
    (void)supported_features; // Features describe action/exporter coverage, not run state.
    const auto& actions = recording.json();
    if (value["recording_id"] != actions["recording_id"])
        detail::invalid("/recording_id", "recording identity mismatch");
    std::map<std::string, std::size_t> action_positions;
    for (std::size_t i = 0; i < actions["actions"].size(); ++i)
        action_positions.emplace(actions["actions"][i]["id"].get<std::string>(), i);
    // Table labels are document-local, so identity cannot be compared by label
    // across boundaries. Each state validates its own reference graph.
    auto check = [&](const nlohmann::json& state, const std::string& path) {
        try {
            if (validate_snapshot) validate_snapshot(state);
            else (void)Snapshot::parse(state);
        }
        catch (const std::invalid_argument& e) { detail::invalid(path, e.what()); }
    };
    check(value["initial"], "/initial");
    if (value["initial"]["boundary_kind"] == "run_complete" && !actions["actions"].empty())
        detail::invalid("/initial", "actions follow terminal state");
    std::size_t next_action = 0;
    for (std::size_t i = 0; i < value["transitions"].size(); ++i) {
        const auto path = "/transitions/" + std::to_string(i);
        const auto& step = value["transitions"][i];
        const auto found = action_positions.find(step["action_id"].get<std::string>());
        if (found == action_positions.end())
            detail::invalid(path + "/action_id", "unknown action identity");
        if (found->second < next_action)
            detail::invalid(path + "/action_id", "duplicate or out-of-order checkpoint");
        next_action = found->second + 1;
        check(step["state"], path + "/state");
        if (step["state"]["boundary_kind"] == "run_complete" && next_action < actions["actions"].size())
            detail::invalid(path + "/state", "actions follow terminal checkpoint");
    }
}
}
