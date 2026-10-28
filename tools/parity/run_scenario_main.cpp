#include "sts2/engine/action.hpp"
#include "sts2/json_io/session.hpp"
#include "sts2/observation/run.hpp"
#include "sts2/persistence/checkpoint.hpp"
#include <iostream>

// Diagnostic scenario transport. Room entry is deliberately separate from the
// public player action surface and never edits generated encounter queues.
int main() {
    using namespace sts2::engine;
    using Json = nlohmann::json;
    RunState state; Scratch scratch; NativeRunExporter exporter;
    for (std::string line; std::getline(std::cin, line);) try {
        const auto request = Json::parse(line); const auto command = request.at("cmd").get<std::string>();
        if (command == "start") {
            NativeRunExporter next_exporter;
            initialize_json_run(state, request.at("setup"), next_exporter.trace());
            exporter = std::move(next_exporter); scratch.trace = exporter.trace();
        } else {
            const auto before = checkpoint(state).bytes;
            (void)observe(state); (void)get_actions(state); (void)describe(observe(state));
            if (checkpoint(state).bytes != before) throw std::runtime_error("Query mutated run");
            RunState restored; if (restore(restored, before, scratch) != RestoreStatus::restored) throw std::runtime_error("Restore failed");
            auto cloned = state;
            auto apply = [&](RunState& run, Scratch& execution) {
                if (command == "scenario_enter_combat") return enter_combat_scenario(run, run.view().boundary, request.at("encounter").get<std::string>(), execution.trace);
                if (command != "action") throw std::invalid_argument("Unknown command");
                const auto& action = request.at("action"); const auto kind = action.at("kind").get<std::string>();
                PlayerInput input;
                if (kind == "play_card") {
                    const auto view = observe(run); const auto index = action.at("card_index").get<std::size_t>();
                    if (!view.combat || index >= view.combat->hand.size()) throw std::invalid_argument("Invalid hand index");
                    if (action.contains("target_index") && action.at("target_index") != 0) throw std::invalid_argument("Invalid target");
                    input = CombatInput{run.view().boundary, CombatInputKind::play_card, view.combat->hand[index], action.contains("target_index")};
                }
                else if (kind == "select_card" || kind == "deselect_card" || kind == "confirm_selection") {
                    const auto view = observe(run);
                    if (!view.combat || !view.combat->selection) throw std::invalid_argument("No active selection");
                    const auto& selection = *view.combat->selection;
                    CardHandle card;
                    if (kind != "confirm_selection") {
                        const auto index = action.at("candidate_index").get<std::size_t>();
                        if (index >= selection.candidates.size()) throw std::invalid_argument("Invalid candidate index");
                        card = selection.candidates[index];
                    }
                    const auto prompt = action.contains("prompt") ? PromptId{action.at("prompt").get<std::uint32_t>()} : selection.id;
                    const auto input_kind = kind == "select_card" ? CombatInputKind::select_card : kind == "deselect_card" ? CombatInputKind::deselect_card : CombatInputKind::confirm_selection;
                    input = CombatInput{run.view().boundary, input_kind, card, false, prompt};
                }
                else if (kind == "end_turn") input = CombatInput{run.view().boundary, CombatInputKind::end_turn};
                else {
                    RunInput choice{run.view().boundary, RunInputKind::skip_rewards};
                    if (kind == "choose_option") { choice.kind = RunInputKind::choose_option; choice.option = action.at("option_index"); }
                    else if (kind == "select_map_node") { choice.kind = RunInputKind::travel; choice.col = action.at("col"); choice.row = action.at("row"); }
                    else if (kind == "take_reward") { choice.kind = RunInputKind::take_reward; choice.option = action.at("index"); }
                    else if (kind == "discard_potion") { choice.kind = RunInputKind::discard_potion; choice.option = action.at("potion_index"); }
                    else if (kind != "skip_rewards") throw std::invalid_argument("Unsupported public action");
                    input = choice;
                }
                if (validate_action(run, input) != Restriction::none) throw std::runtime_error("Public input is not legal");
                return apply_action(run, input, execution);
            };
            Scratch branch_scratch;
            const auto result = apply(state, scratch);
            if (result.status != StepStatus::awaiting_input && result.status != StepStatus::player_died) throw std::runtime_error("Operation failed");
            if (apply(restored, branch_scratch).status != result.status || apply(cloned, branch_scratch).status != result.status || restored != state || cloned != state)
                throw std::runtime_error("Clone/restore future differs");
        }
        Json response{{"ok", true}, {"state", exporter.snapshot(state)}, {"selection", nullptr}};
        const auto view = observe(state);
        if (view.combat && view.combat->selection) {
            const auto& selection = *view.combat->selection;
            response["selection"] = {{"prompt", selection.id.value}, {"minimum", selection.minimum}, {"maximum", selection.maximum},
                {"automatic_at_maximum", selection.submits_immediately}, {"selected_count", selection.selected.size()}};
        }
        std::cout << response.dump() << std::endl;
    } catch (const std::exception& error) {
        std::cout << Json{{"ok", false}, {"error", error.what()}}.dump() << std::endl;
    }
}
