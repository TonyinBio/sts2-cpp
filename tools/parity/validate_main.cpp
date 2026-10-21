#include "recording.hpp"
#include "snapshot.hpp"
#include "state_recording.hpp"
#include <iostream>
#include <stdexcept>

int main() {
    using namespace sts2::parity;
    using Json = nlohmann::json;
    const std::set<std::string> features = {"basic_combat", "ironclad_starter_shrinker_v1", "run_flow_v1"};
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
            if (kind == "actions") (void)ActionRecording::parse(request.at("value"), features);
            else if (kind == "snapshot") {
                if (request.at("value").is_string()) (void)Snapshot::parse_text(request.at("value").get<std::string>());
                else (void)Snapshot::parse(request.at("value"));
            }
            else if (kind == "states") validate_state_recording(request.at("value"),
                ActionRecording::parse(request.at("actions"), features), features);
            else throw std::invalid_argument("Unknown validation kind");
            std::cout << Json{{"valid", true}}.dump() << std::endl;
        } catch (const std::exception& e) {
            std::cout << Json{{"valid", false}, {"error", e.what()}}.dump() << std::endl;
        }
    }
}
