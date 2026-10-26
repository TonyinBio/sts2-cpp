#include "sts2/kernel/simulator.hpp"
#include "sts2/observation/observe.hpp"
#include "sts2/persistence/checkpoint.hpp"
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>

using namespace sts2::engine;
namespace {
void display(const State& state) {
    const auto view = observe(state);
    std::cout << "Turn " << view.turn << " | HP " << view.hp << '/' << view.max_hp << " | Block " << view.block
              << " | Energy " << view.energy << " | Beetle " << view.enemy_hp << '/' << view.enemy_max_hp;
    if (view.phase == Phase::play) std::cout << " | Intent " << (view.intent == BeetleMove::shrink ? "Shrink" : std::to_string(view.enemy_attack) + " damage");
    std::cout << '\n';
    if (view.phase == Phase::combat_won) { std::cout << "Combat won.\n"; return; }
    if (view.phase == Phase::player_died) { std::cout << "Player died.\n"; return; }
    if (view.phase == Phase::invalid) { std::cout << "Simulation invalid; restore or restart.\n"; return; }
    if (view.phase == Phase::discard_choice) std::cout << "Choose a card to discard with select INDEX.\n";
    for (std::size_t i = 0; i < view.hand.size(); ++i) {
        const auto card = observe_card(state, view.hand[i]);
        std::cout << i << ": " << card->name << (card->upgraded ? "+" : "") << " (" << card->cost << ") " << describe(*card) << '\n';
    }
    std::cout << "Draw " << view.draw_count << " | Discard " << view.discard_count << '\n';
}
}
int main(int argc, char** argv) try {
    std::string seed = "ORACLECHECK01"; bool autoplay = false;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--seed" && i + 1 < argc) seed = argv[++i];
        else if (arg == "--auto") autoplay = true;
        else { std::cerr << "Usage: sts2_combat [--seed SEED] [--auto]\n"; return 1; }
    }
    State state; Scratch scratch; Simulator simulator; CombatConfig config; config.seed = seed;
    if (simulator.initialize(state, config, scratch).status != StepStatus::awaiting_input) return 1;
    std::cout << "Slay the Spire 2 " << game_build << " isolated combat kernel\n"
              << "Ironclad / Shrinker Beetle A0. Full-run generation and rewards are unsupported.\n"
              << "Commands: play INDEX, end, select INDEX, save PATH, load PATH, quit\n";
    for (unsigned steps = 0; steps < 10000; ++steps) {
        display(state);
        if (state.view().phase == Phase::combat_won || state.view().phase == Phase::player_died) return 0;
        Input input{state.view().boundary, InputKind::end_turn};
        if (autoplay) {
            ActionSpaceView actions(state);
            // Prefer damage, then any playable card. This is a fixed example
            // policy, not a legality shortcut or game rule.
            bool found = false;
            for (std::size_t i = 0; i < state.zone(Zone::hand).size(); ++i) {
                const auto action = *actions.at(i);
                const auto card = observe_card(state, action.input.card);
                if (action.restriction == Restriction::none && card->enemy_target) { input = action.input; found = true; break; }
            }
            if (!found) for (std::size_t i = 0; i < actions.size(); ++i) {
                const auto action = *actions.at(i);
                if (action.restriction == Restriction::none) { input = action.input; break; }
            }
        } else {
            std::cout << "> " << std::flush;
            std::string line; if (!std::getline(std::cin, line)) return 0;
            std::istringstream words(line); std::string command; words >> command;
            if (command == "quit") return 0;
            if (command == "save" || command == "load") {
                std::string path; std::getline(words >> std::ws, path);
                if (path.empty()) { std::cout << "A path is required.\n"; continue; }
                if (command == "save") {
                    const auto saved = checkpoint(state); std::ofstream out(path, std::ios::binary);
                    out.write(reinterpret_cast<const char*>(saved.bytes.data()), static_cast<std::streamsize>(saved.bytes.size()));
                    std::cout << (out ? "Checkpoint saved.\n" : "Checkpoint write failed.\n");
                } else {
                    std::ifstream in(path, std::ios::binary);
                    const std::vector<char> raw{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
                    const auto status = restore(state, {reinterpret_cast<const std::byte*>(raw.data()), raw.size()}, scratch);
                    std::cout << (status == RestoreStatus::restored ? "Checkpoint restored.\n" : "Checkpoint rejected.\n");
                }
                continue;
            }
            if (command == "play" || command == "select") {
                std::size_t index; std::string extra;
                if (!(words >> index) || words >> extra || index >= state.zone(Zone::hand).size()) { std::cout << "Invalid card index.\n"; continue; }
                input.card = state.zone(Zone::hand)[index];
                input.kind = command == "play" ? InputKind::play_card : InputKind::select_card;
                input.target_enemy = command == "play" && observe_card(state, input.card)->enemy_target;
            } else if (command == "end") {
                std::string extra;
                if (words >> extra) { std::cout << "end takes no arguments.\n"; continue; }
            } else { std::cout << "Unknown command.\n"; continue; }
        }
        const auto result = simulator.apply(state, input, scratch);
        if (result.status == StepStatus::invalid_action) {
            if (result.reason == Restriction::insufficient_energy) std::cout << "Not enough Energy.\n";
            else if (result.reason == Restriction::wrong_phase) std::cout << "Finish the current card selection first.\n";
            else std::cout << "That action is unavailable.\n";
        }
        else if (result.status == StepStatus::engine_error || result.status == StepStatus::resource_limit) { std::cerr << "Simulation failed.\n"; return 2; }
    }
    std::cerr << "Command limit reached.\n"; return 2;
} catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
