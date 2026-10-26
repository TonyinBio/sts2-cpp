#include "sts2/parity/replay.hpp"
#include "sts2/integration/session.hpp"
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>

namespace {
std::string read(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("Cannot read " + path);
    return {std::istreambuf_iterator<char>(input), {}};
}
}
int main(int argc, char** argv) {
    using Json = nlohmann::json;
    try {
        std::map<std::string, std::string> paths;
        for (int i = 1; i < argc; i += 2) {
            if (i + 1 == argc || (std::string(argv[i]) != "--actions" && std::string(argv[i]) != "--states" && std::string(argv[i]) != "--reference") || !paths.emplace(argv[i], argv[i + 1]).second)
                throw std::invalid_argument("Usage: sts2_replay --actions actions.json --states states.json --reference reference.json");
        }
        if (paths.size() != 3) throw std::invalid_argument("Supply --actions, --states and independent --reference metadata");
        sts2::engine::Session session;
        sts2::parity::ReplayBackend backend{
            [&](const Json& setup, const sts2::parity::Snapshot&) { session.initialize(setup); },
            [&](const Json& action) {
                const auto result = session.apply(action);
                using sts2::engine::StepStatus;
                if (result.status != StepStatus::awaiting_input && result.status != StepStatus::combat_won && result.status != StepStatus::player_died)
                    throw std::runtime_error("Simulator action failed: status=" + std::to_string(static_cast<unsigned>(result.status)) + " restriction=" + std::to_string(static_cast<unsigned>(result.reason)));
            },
            [&] { return sts2::parity::Snapshot::parse(session.snapshot()); }};
        const auto actions = read(paths.at("--actions"));
        const auto failures = sts2::parity::replay_parity(actions, read(paths.at("--states")),
            {"basic_combat", "ironclad_starter_shrinker_v1", "run_lifecycle", "run_flow_v1"}, Json::parse(read(paths.at("--reference"))), backend);
        if (!failures.empty()) {
            for (const auto& failure : failures) for (const auto& difference : failure.differences)
                std::cerr << failure.checkpoint << ' ' << difference.path << " expected=" << difference.expected << " actual=" << difference.actual << '\n';
            return 2;
        }
        std::cout << "Gameplay comparison revision 2 replay passed: " << Json::parse(actions).at("actions").size() << " actions\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Simulator replay failed: " << error.what() << '\n'; return 1;
    }
}
