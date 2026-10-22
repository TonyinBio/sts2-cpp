#include "recording.hpp"
#include "snapshot.hpp"
#include "state_recording.hpp"
#include <iostream>
#include <stdexcept>
#include <optional>
#include <unordered_set>

int main() {
    using namespace sts2::parity;
    using Json = nlohmann::json;
    const std::set<std::string> features = {"basic_combat", "ironclad_starter_shrinker_v1", "run_flow_v1"};
    // Cache by serialized content, never by caller-controlled checkpoint labels.
    // Only successfully validated content enters either cache.
    std::optional<ActionRecording> cached_actions;
    std::string cached_action_content, latest_snapshot;
    std::unordered_set<std::string> snapshots;
    auto parse_actions = [&](const Json& value) -> const ActionRecording& {
        auto content = value.dump();
        if (!cached_actions || cached_action_content != content) {
            auto parsed = ActionRecording::parse(value, features);
            cached_actions = std::move(parsed);
            cached_action_content = std::move(content);
        }
        return *cached_actions;
    };
    auto validate_snapshot = [&](const Json& value) {
        auto content = value.dump();
        if (!snapshots.contains(content)) {
            (void)Snapshot::parse(value);
            snapshots.insert(content);
        }
        return content;
    };
    std::string line;
    while (std::getline(std::cin, line)) {
        try {
            const auto request = Json::parse(line);
            const auto kind = request.at("kind").get<std::string>();
            if (kind == "compare") {
                const auto expected = Snapshot::parse_text(request.at("expected").get<std::string>());
                const auto actual = Snapshot::parse_text(request.at("actual").get<std::string>());
                const auto differences = compare(expected, actual);
                Json result = {{"valid", true}, {"equal", differences.empty()}};
                if (!differences.empty()) result["difference"] = {
                    {"path", differences.front().path}, {"expected", differences.front().expected},
                    {"actual", differences.front().actual}};
                std::cout << result.dump() << std::endl;
                continue;
            }
            if (kind == "actions") (void)parse_actions(request.at("value"));
            else if (kind == "snapshot") {
                const auto& input = request.at("value");
                const auto value = input.is_string() ? Json::parse(input.get<std::string>()) : input;
                auto content = value.dump();
                if (!snapshots.contains(content)) {
                    if (input.is_string()) (void)Snapshot::parse_text(input.get<std::string>());
                    else (void)Snapshot::parse(value);
                    snapshots.insert(content);
                }
                latest_snapshot = std::move(content);
            }
            else if (kind == "states") {
                std::unordered_set<std::string> retained;
                validate_state_recording(request.at("value"), parse_actions(request.at("actions")), features,
                    [&](const Json& state) {
                        retained.insert(validate_snapshot(state));
                    });
                // Do not retain every unsampled boundary for the entire run.
                std::erase_if(snapshots, [&](const auto& content) {
                    return content != latest_snapshot && !retained.contains(content);
                });
            }
            else throw std::invalid_argument("Unknown validation kind");
            std::cout << Json{{"valid", true}}.dump() << std::endl;
        } catch (const std::exception& e) {
            std::cout << Json{{"valid", false}, {"error", e.what()}}.dump() << std::endl;
        }
    }
}
