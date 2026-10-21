#include "sts2/integration/session.hpp"
#include <iostream>
#include <string>

int main() {
    using Json = nlohmann::json;
    sts2::engine::Session session;
    for (std::string line; std::getline(std::cin, line);) {
        try {
            const auto request = Json::parse(line);
            const auto command = request.at("cmd").get<std::string>();
            if (command == "start") session.initialize(request.at("setup"));
            else if (command == "action") {
                const auto result = session.apply(request.at("action"));
                using sts2::engine::StepStatus;
                if (result.status != StepStatus::awaiting_input && result.status != StepStatus::combat_won && result.status != StepStatus::player_died) {
                    std::cout << Json{{"ok", false}, {"status", static_cast<unsigned>(result.status)},
                        {"restriction", static_cast<unsigned>(result.reason)}, {"boundary", result.boundary.value}}.dump() << '\n';
                    continue;
                }
            } else if (command != "snapshot") throw std::invalid_argument("Expected start, action, or snapshot command");
            std::cout << Json{{"ok", true}, {"boundary", session.state().boundary.value}, {"state", session.snapshot()}}.dump() << '\n';
        } catch (const std::exception& error) {
            std::cout << Json{{"ok", false}, {"error", error.what()}}.dump() << '\n';
        }
    }
    return std::cin.bad() ? 1 : 0;
}
