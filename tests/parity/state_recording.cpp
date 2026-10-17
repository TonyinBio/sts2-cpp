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
    const auto& actions = recording.json();
    if (value["recording_id"] != actions["recording_id"])
        detail::invalid("/recording_id", "recording identity mismatch");
    if (value["transitions"].size() != actions["actions"].size())
        detail::invalid("/transitions", "expected exactly one state per action");
    std::map<std::string, std::string> identities;
    auto check = [&](const nlohmann::json& state, const std::string& path) {
        try { (void)Snapshot::parse(state, supported_features); }
        catch (const std::invalid_argument& e) { detail::invalid(path, e.what()); }
        if (state["required_features"] != actions["required_features"])
            detail::invalid(path + "/required_features", "features differ from action recording");
        auto identity = [&](const nlohmann::json& object) {
            const auto id = object["instance_id"].get<std::string>();
            const auto model = object["model_id"].get<std::string>();
            const auto [it, inserted] = identities.emplace(id, model);
            if (!inserted && it->second != model) detail::invalid(path, "instance identity changed model");
        };
        identity(state["player"]);
        for (const auto& p : state["player"]["powers"]) identity(p);
        for (const auto& r : state["player"]["relics"]) identity(r);
        for (const auto& e : state["enemies"]) {
            identity(e);
            for (const auto& p : e["powers"]) identity(p);
        }
        for (const auto& c : state["cards"]) identity(c);
    };
    check(value["initial"], "/initial");
    auto boundary = value["initial"]["boundary"];
    for (std::size_t i = 0; i < value["transitions"].size(); ++i) {
        const auto path = "/transitions/" + std::to_string(i);
        if (boundary == "combat_end") detail::invalid(path, "action follows terminal state");
        const auto& step = value["transitions"][i];
        if (step["action_id"] != actions["actions"][i]["id"])
            detail::invalid(path + "/action_id", "action identity/order mismatch");
        check(step["state"], path + "/state");
        boundary = step["state"]["boundary"];
    }
}
}
