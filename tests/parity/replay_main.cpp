#include "simulator_adapter.hpp"
#include "sha256.hpp"
#include <iterator>
#include <fstream>
#include <iostream>

int main(int argc, char** argv) {
    if (argc != 3) { std::cerr << "Usage: parity_replay actions.json states.json\n"; return 2; }
    try {
        std::ifstream a(argv[1], std::ios::binary), s(argv[2]);
        const std::string bytes{std::istreambuf_iterator<char>(a), std::istreambuf_iterator<char>()};
        const auto actions = nlohmann::json::parse(bytes);
        const auto states = nlohmann::json::parse(s);
        if (states.at("actions_sha256") != sts2::parity::sha256(bytes))
            throw std::invalid_argument("Action-file SHA-256 does not match reference recording");
        sts2::parity::replay_and_compare(actions, states);
        std::cout << "PASS: " << actions["actions"].size() << " transitions match reference\n";
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
