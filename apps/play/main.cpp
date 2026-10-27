#include "sts2/observation/run.hpp"
#include <charconv>
#include <algorithm>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
    using namespace sts2::engine;
    SeededConfig config; config.seed = argc > 1 ? argv[1] : "KERNELRUN14";
    RunState run; Scratch scratch;
    if (initialize_run(run, config).status != StepStatus::awaiting_input) return 1;
    for (;;) {
        const auto view = observe(run);
        std::cout << describe(view) << "\nHP " << view.hp << '/' << view.max_hp << "  Gold " << view.gold << '\n';
        for (const auto& card : view.hand_cards)
            std::cout << "  " << card.name << " (" << card.cost << " Energy): " << describe(card) << '\n';
        for (const auto& option : view.options) {
            const auto text = describe(option);
            std::cout << "  Gift " << option.id << ": " << text.name << " — " << text.description << '\n';
        }
        for (const auto& reward : view.rewards) {
            std::cout << "  Reward " << reward.id << ": ";
            if (reward.kind == RewardKind::gold) std::cout << reward.gold << " gold";
            else if (reward.kind == RewardKind::potion) std::cout << reward.model_id;
            else std::cout << "card choice";
            std::cout << (reward.claimed ? " (claimed)\n" : "\n");
        }
        for (const auto& card : view.reward_cards) {
            const auto text = describe(card);
            std::cout << "  Card " << card.instance << ": " << text.name << " (" << card.energy_cost.value_or(-1) << " Energy) — " << text.description << '\n';
        }
        const auto offered = actions(run);
        if (offered.empty()) return view.valid ? 0 : 1;
        for (std::size_t i = 0; i < offered.size(); ++i) {
            std::cout << i << ": ";
            if (const auto* input = std::get_if<CombatInput>(&offered[i].input)) {
                if (input->kind == CombatInputKind::end_turn) std::cout << "End turn";
                else {
                    const auto card = std::find_if(view.hand_cards.begin(), view.hand_cards.end(), [&](const auto& c) { return c.handle == input->card; });
                    std::cout << (input->kind == CombatInputKind::play_card ? "Play " : "Discard ") << card->name;
                }
            } else {
                const auto& input_run = std::get<RunInput>(offered[i].input);
                switch (input_run.kind) {
                case RunInputKind::choose_option: std::cout << "Choose gift " << input_run.option; break;
                case RunInputKind::travel: std::cout << "Travel to " << input_run.col << ',' << input_run.row; break;
                case RunInputKind::take_reward: std::cout << "Claim reward " << input_run.option; break;
                case RunInputKind::choose_reward_card: std::cout << "Choose card " << input_run.option; break;
                case RunInputKind::reward_alternative: std::cout << "Skip card"; break;
                case RunInputKind::skip_rewards: std::cout << "Leave rewards"; break;
                case RunInputKind::discard_potion: std::cout << "Discard potion in slot " << input_run.option; break;
                default: break;
                }
            }
            if (offered[i].restriction != Restriction::none) std::cout << " — " << describe(offered[i].restriction);
            std::cout << '\n';
        }
        std::cout << "> " << std::flush;
        std::string line; if (!std::getline(std::cin, line) || line == "quit") return 0;
        std::size_t selected = 0;
        const auto parsed = std::from_chars(line.data(), line.data() + line.size(), selected);
        if (parsed.ec != std::errc{} || parsed.ptr != line.data() + line.size() || selected >= offered.size()) continue;
        const auto result = apply_player(run, offered[selected].input, scratch);
        if (result.reason != Restriction::none) std::cout << describe(result.reason) << '\n';
    }
}
