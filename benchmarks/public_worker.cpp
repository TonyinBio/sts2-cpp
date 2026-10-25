#include "sts2/integration/session.hpp"
#include "sts2/observation/run.hpp"
#include <algorithm>
#include "sts2/persistence/checkpoint.hpp"
#include <iostream>

// Benchmark-scoped transport; this is not a stable public JSON contract.
namespace {
using namespace sts2::engine;
using Json = nlohmann::json;
Json project(const RunState& state, Json& capabilities) {
    const auto view = observe(state);
    Json result{{"profile", "warm-public-structured-v1"}, {"hp", view.hp}, {"max_hp", view.max_hp}, {"gold", view.gold},
        {"deck", Json::array()}, {"relics", Json::array()}, {"potions", Json::array()},
        {"choices", Json::array()}, {"combat", nullptr}};
    for (const auto& card : view.deck) result["deck"].push_back({{"instance", &card - view.deck.data()}, {"id", card.model_id}, {"upgrade", card.upgrade}, {"energy_cost", card.energy_cost ? Json(*card.energy_cost) : Json(nullptr)}});
    for (const auto& relic : view.relics) result["relics"].push_back({{"id", relic.id}, {"model_id", relic.model_id}});
    for (const auto& potion : view.potions) result["potions"].push_back({{"id", potion.id}, {"model_id", potion.model_id}});
    result["contract_revision"] = view.contract_revision;
    result["build"] = view.build; result["valid"] = view.valid;
    result["character"] = view.character; result["seed"] = view.seed;
    result["ascension"] = view.ascension; result["act"] = view.act; result["potion_capacity"] = view.potion_capacity;
    result["minimum_selection"] = view.minimum_selection; result["maximum_selection"] = view.maximum_selection;
    result["submits_immediately"] = view.submits_immediately;
    result["selecting_reward_card"] = view.selecting_reward_card; result["can_skip_rewards"] = view.can_skip_rewards;
    result["can_skip_card_reward"] = view.can_skip_card_reward;
    const auto coord = [&](MapNodeId id) -> Json {
        const auto node = std::find_if(view.map.begin(), view.map.end(), [&](const auto& n) { return n.id == id; });
        if (node == view.map.end()) throw std::runtime_error("Missing public map identity");
        return Json::array({node->col, node->row});
    };
    constexpr std::array<std::string_view, 9> map_names{"Unassigned", "Unknown", "Shop", "Treasure", "RestSite", "Monster", "Elite", "Boss", "Ancient"};
    result["map"] = Json::array(); result["path"] = Json::array();
    for (const auto& node : view.map) {
        Json children = Json::array(); for (auto id : node.children) children.push_back(coord(id));
        result["map"].push_back({{"coord", coord(node.id)}, {"kind", map_names[static_cast<unsigned>(node.kind)]},
            {"children", children}, {"visited", node.visited}, {"current", node.current}, {"available", node.available}});
    }
    result["options"] = Json::array(); for (const auto& option : view.options) result["options"].push_back({{"id", option.id}, {"model_id", option.model_id}});
    for (auto id : view.path) result["path"].push_back(coord(id));
    result["rewards"] = Json::array(); result["reward_cards"] = Json::array();
    for (const auto& reward : view.rewards) result["rewards"].push_back({{"id", reward.id}, {"kind", static_cast<unsigned>(reward.kind)},
        {"gold", reward.gold}, {"model_id", reward.model_id}, {"claimed", reward.claimed}});
    for (const auto& card : view.reward_cards) result["reward_cards"].push_back({{"id", card.instance}, {"model_id", card.model_id},
        {"upgrade", card.upgrade}, {"energy_cost", card.energy_cost ? Json(*card.energy_cost) : Json(nullptr)}});
    switch (view.prompt) {
    case PublicPrompt::neow: result["prompt"] = "neow"; break;
    case PublicPrompt::map: result["prompt"] = "map"; break;
    case PublicPrompt::combat: result["prompt"] = "combat"; break;
    case PublicPrompt::rewards: result["prompt"] = "rewards"; break;
    default: throw std::runtime_error("Unsupported benchmark prompt");
    }
    result["run_phase"] = result["prompt"];
    for (const auto& offered : actions(state)) {
        capabilities.push_back(static_cast<unsigned>(offered.restriction));
        // unsupported_setup describes engine capability, not native legality.
        const auto legality = offered.restriction == Restriction::unsupported_setup ? Restriction::none : offered.restriction;
        Json action{{"enabled", legality == Restriction::none}, {"restriction", static_cast<unsigned>(legality)}};
        if (const auto* input = std::get_if<Input>(&offered.input)) {
            if (input->kind == InputKind::end_turn) action["kind"] = "end_turn";
            else {
                if (input->kind != InputKind::play_card) throw std::runtime_error("Unsupported benchmark selection");
                action["kind"] = "play_card"; action["target_enemy"] = input->target_enemy;
                const auto found = std::find(view.combat->hand.begin(), view.combat->hand.end(), input->card);
                action["index"] = std::distance(view.combat->hand.begin(), found);
            }
        } else {
            const auto& run_input = std::get<RunInput>(offered.input);
            switch (run_input.kind) {
            case RunInputKind::choose_option: action["kind"] = "choose_option"; action["index"] = run_input.option; break;
            case RunInputKind::travel: action["kind"] = "select_map_node"; action["col"] = run_input.col; action["row"] = run_input.row; break;
            case RunInputKind::take_reward: action["kind"] = "take_reward"; action["index"] = run_input.option; break;
            case RunInputKind::skip_rewards: action["kind"] = "skip_rewards"; break;
            case RunInputKind::discard_potion: action["kind"] = "discard_potion"; action["index"] = run_input.option; break;
            default: throw std::runtime_error("Unsupported benchmark action");
            }
        }
        result["choices"].push_back(action);
    }
    result["draw_cards"] = Json::array(); result["discard_cards"] = Json::array(); result["exhaust_cards"] = Json::array();
    if (view.combat) {
        const auto& combat = *view.combat;
        const auto cards = [](const auto& pile) {
            Json result = Json::array();
            for (const auto& card : pile) result.push_back({{"instance", card.handle.slot}, {"id", card.model_id}, {"cost", card.cost}, {"upgraded", card.upgraded},
                {"damage", card.damage}, {"block", card.block}, {"vulnerable", card.vulnerable}, {"enemy_target", card.enemy_target}});
            return result;
        };
        const auto hand = cards(view.hand_cards);
        result["draw_cards"] = cards(view.draw); result["discard_cards"] = cards(view.discard); result["exhaust_cards"] = cards(view.exhaust);
        result["combat"] = {{"phase", combat.phase == Phase::play ? "play" : "unsupported"}, {"hp", combat.hp}, {"max_hp", combat.max_hp}, {"block", combat.block}, {"energy", combat.energy},
            {"turn", combat.turn}, {"enemy_vulnerable", combat.enemy_vulnerable}, {"enemy_attack", combat.enemy_attack}, {"shrunk", combat.shrunk}, {"intent", static_cast<unsigned>(combat.intent)}, {"enemy_hp", combat.enemy_hp}, {"enemy_max_hp", combat.enemy_max_hp},
            {"draw", combat.draw_count}, {"discard", combat.discard_count}, {"exhaust", combat.exhaust_count}, {"hand", hand}};
    }
    return result;
}
}

// Room entry remains an explicitly declared diagnostic scenario operation.
int main() {
    using namespace sts2::engine;
    using Json = nlohmann::json;
    RunState state; Scratch scratch;
    for (std::string line; std::getline(std::cin, line);) try {
        const auto request = Json::parse(line); const auto command = request.at("cmd").get<std::string>();
        if (command == "reset") {
            RunState empty; state = std::move(empty); scratch = Scratch{};
            std::cout << Json{{"reset", true}, {"type", "oracle_reset"}, {"singleton_listeners", Json::object()}}.dump() << std::endl;
            continue;
        }
        if (command == "start") {
            Session session; session.initialize(request.at("setup")); state = session.state();
        } else if (command == "query_audit") {
            const auto before = checkpoint(state).bytes;
            Json capabilities = Json::array(); const auto observation = project(state, capabilities);
            std::cout << Json{{"checkpoint_unchanged", checkpoint(state).bytes == before}, {"observation", observation}}.dump() << std::endl;
            continue;
        } else if (command == "capability_rejection") {
            const auto before = checkpoint(state).bytes;
            const auto result = apply_player(state, RunInput{state.boundary, RunInputKind::choose_option,
                request.at("option_index").get<std::uint32_t>()}, scratch);
            std::cout << Json{{"ok", true}, {"unsupported", result.status == StepStatus::unsupported_content},
                {"restriction", static_cast<unsigned>(result.reason)}, {"checkpoint_unchanged", checkpoint(state).bytes == before}}.dump() << std::endl;
            continue;
        } else if (command != "query") {
            auto apply = [&](RunState& run) {
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
            if (result.status != StepStatus::awaiting_input && result.status != StepStatus::player_died) throw std::runtime_error("Operation failed");

        }
        Json capabilities = Json::array();
        const auto observation = project(state, capabilities);
        std::cout << Json{{"ok", true}, {"observation", observation}, {"capabilities", capabilities}}.dump() << std::endl;
    } catch (const std::exception& error) {
        std::cout << Json{{"ok", false}, {"error", error.what()}}.dump() << std::endl;
    }
}
