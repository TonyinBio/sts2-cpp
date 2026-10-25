#include "sts2/integration/session.hpp"
#include "sts2/observation/run.hpp"
#include "sts2/persistence/checkpoint.hpp"
#include <iostream>
#include <algorithm>

// Diagnostic scenario transport. Room entry is deliberately separate from the
// public player action surface and never edits generated encounter queues.
int main() {
    using namespace sts2::engine;
    using Json = nlohmann::json;
    RunState state; Scratch scratch; NativeRunExporter exporter;
    for (std::string line; std::getline(std::cin, line);) try {
        const auto request = Json::parse(line); const auto command = request.at("cmd").get<std::string>();
        if (command == "start") {
            if (request.contains("scenario_deck")) {
                const auto& setup = request.at("setup");
                std::vector<CardSpec> deck;
                for (const auto& entry : request.at("scenario_deck")) {
                    CardId id = CardId::count;
                    for (unsigned i = 0; i < static_cast<unsigned>(CardId::count); ++i)
                        if (Ruleset::pinned().card(static_cast<CardId>(i))->model_id == entry.at("id")) id = static_cast<CardId>(i);
                    if (id == CardId::count) throw std::invalid_argument("Unsupported scenario card");
                    deck.push_back({id, entry.value("upgraded", false)});
                }
                if (deck.empty()) throw std::invalid_argument("Empty scenario deck");
                std::vector<RelicId> relics;
                if (request.contains("scenario_relics")) for (const auto& name : request.at("scenario_relics")) {
                    constexpr std::array names{"RELIC.BURNING_BLOOD", "RELIC.GOLDEN_PEARL", "RELIC.CHARONS_ASHES", "RELIC.JOSS_PAPER"};
                    const auto found = std::find(names.begin(), names.end(), name.get<std::string>());
                    if (found == names.end()) throw std::invalid_argument("Unsupported scenario relic");
                    relics.push_back(static_cast<RelicId>(found - names.begin()));
                }
                const auto seed = setup.at("seed").get<std::string>();
                SeededConfig config; config.seed = seed;
                config.clock.epoch_seconds = setup.at("clock").at("epoch_seconds");
                config.clock.seconds_per_action = setup.at("clock").at("seconds_per_action");
                if (initialize_combat_scenario(state, config, deck, relics).status != StepStatus::awaiting_input)
                    throw std::invalid_argument("Unsupported scenario setup");
                if (request.contains("scenario_enemy_hp")) {
                    const auto hp = request.at("scenario_enemy_hp").get<int>();
                    if (hp < 1 || hp > 40) throw std::invalid_argument("Unsupported scenario HP");
                    state.combat->enemy.hp = hp; state.combat->enemy.max_hp = hp;
                }
            } else { Session session; session.initialize(request.at("setup")); state = session.state(); }
        } else {
            const auto before = checkpoint(state).bytes;
            (void)observe(state); (void)actions(state); (void)describe(observe(state));
            if (checkpoint(state).bytes != before) throw std::runtime_error("Query mutated run");
            RunState restored; if (restore(restored, before, scratch) != RestoreStatus::restored) throw std::runtime_error("Restore failed");
            auto cloned = state;
            auto apply = [&](RunState& run) {
                if (command == "scenario_card_played_probe") {
                    if (!run.combat) throw std::invalid_argument("No combat");
                    const auto index = request.at("card_index").get<std::size_t>();
                    if (index >= run.combat->zone(Zone::hand).size() || run.combat->plays.empty()) throw std::invalid_argument("Invalid probe source");
                    const auto started = begin_card_played_probe(*run.combat, run.boundary, static_cast<std::uint32_t>(run.combat->plays.size() - 1), run.combat->zone(Zone::hand)[index], request.at("power_index").get<std::uint32_t>());
                    if (started.status != StepStatus::awaiting_input) return started;
                    return Simulator{}.advance(*run.combat, scratch);
                }
                if (command == "scenario_duration") {
                    if (!run.combat) throw std::invalid_argument("No combat");
                    const auto operation = request.at("operation").get<std::string>();
                    if (operation == "capture") return capture_duration_scenario(*run.combat, run.boundary);
                    if (operation == "step") return step_duration_scenario(*run.combat, run.boundary);
                    if (operation == "remove") return remove_power_scenario(*run.combat, run.boundary, request.at("index").get<std::uint32_t>());
                    throw std::invalid_argument("Unsupported duration probe operation");
                }
                if (command == "scenario_power") {
                    if (!run.combat) throw std::invalid_argument("No combat");
                    constexpr std::array names{"POWER.SHRINK_POWER", "POWER.VULNERABLE_POWER", "POWER.STRENGTH_POWER", "POWER.DEXTERITY_POWER", "POWER.WEAK_POWER", "POWER.ARTIFACT_POWER"};
                    auto at = std::find(names.begin(), names.end(), request.at("id").get<std::string>());
                    if (at == names.end()) throw std::invalid_argument("Unsupported power");
                    return apply_power_scenario(*run.combat, run.boundary, {static_cast<PowerId>(at - names.begin()), request.at("amount").get<int>(), request.value("enemy_owner", false), request.value("applier_enemy", false), request.value("has_applier", true)});
                }
                if (command == "scenario_enter_combat") return enter_combat_scenario(run, run.boundary, request.at("encounter").get<std::string>());
                if (command != "action") throw std::invalid_argument("Unknown command");
                const auto& action = request.at("action"); const auto kind = action.at("kind").get<std::string>();
                PlayerInput input;
                if (kind == "play_card") {
                    const auto view = observe(run); const auto index = action.at("card_index").get<std::size_t>();
                    if (!view.combat || index >= view.combat->hand.size()) throw std::invalid_argument("Invalid hand index");
                    if (action.contains("target_index") && action.at("target_index") != 0) throw std::invalid_argument("Invalid target");
                    input = Input{run.boundary, InputKind::play_card, view.combat->hand[index], action.contains("target_index")};
                }
                else if (kind == "end_turn") input = Input{run.boundary, InputKind::end_turn};
                else {
                    RunInput choice{run.boundary, RunInputKind::skip_rewards};
                    if (kind == "choose_option") { choice.kind = RunInputKind::choose_option; choice.option = action.at("option_index"); }
                    else if (kind == "select_map_node") { choice.kind = RunInputKind::travel; choice.col = action.at("col"); choice.row = action.at("row"); }
                    else if (kind == "take_reward") { choice.kind = RunInputKind::take_reward; choice.option = action.at("index"); }
                    else if (kind == "discard_potion") { choice.kind = RunInputKind::discard_potion; choice.option = action.at("potion_index"); }
                    else if (kind != "skip_rewards") throw std::invalid_argument("Unsupported public action");
                    input = choice;
                }
                if (validate_player(run, input) != Restriction::none) throw std::runtime_error("Public input is not legal");
                return apply_player(run, input, scratch);
            };
            const auto result = apply(state);
            if (result.status != StepStatus::awaiting_input && result.status != StepStatus::player_died && result.status != StepStatus::combat_won) throw std::runtime_error("Operation failed");
            if (apply(restored).status != result.status || apply(cloned).status != result.status || restored != state || cloned != state)
                throw std::runtime_error("Clone/restore future differs");
        }
        Json reply{{"ok", true}, {"state", exporter.snapshot(state)}};
        if (command == "scenario_card_played_probe") {
            const auto name = [&](ListenerRef listener) -> std::string {
                constexpr std::array power_names{"POWER.SHRINK_POWER", "POWER.VULNERABLE_POWER", "POWER.STRENGTH_POWER", "POWER.DEXTERITY_POWER", "POWER.WEAK_POWER", "POWER.ARTIFACT_POWER"};
                constexpr std::array relic_names{"RELIC.BURNING_BLOOD", "RELIC.GOLDEN_PEARL", "RELIC.CHARONS_ASHES", "RELIC.JOSS_PAPER"};
                switch (listener.kind) {
                case ListenerKind::power: return power_names[static_cast<std::size_t>(state.combat->powers[listener.index].definition)];
                case ListenerKind::relic: return relic_names[static_cast<std::size_t>(state.combat->relics[listener.index].definition)];
                case ListenerKind::card: return std::string(Ruleset::pinned().card(state.combat->cards.get({listener.index, listener.generation})->definition)->model_id);
                case ListenerKind::monster: return "MONSTER.SHRINKER_BEETLE";
                }
                throw std::invalid_argument("Unknown probe listener");
            };
            Json ordinary = Json::array(), late = Json::array();
            for (auto listener : state.combat->card_played_dispatch.visited[0]) ordinary.push_back(name(listener));
            for (auto listener : state.combat->card_played_dispatch.visited[1]) late.push_back(name(listener));
            reply["dispatch_trace"] = {{"ordinary", ordinary}, {"late", late}};
        }
        std::cout << reply.dump() << std::endl;
    } catch (const std::exception& error) {
        std::cout << Json{{"ok", false}, {"error", error.what()}}.dump() << std::endl;
    }
}
