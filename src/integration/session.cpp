#include "sts2/integration/session.hpp"
#include <algorithm>
#include <charconv>
#include <limits>
#include <stdexcept>

namespace sts2::engine {
namespace {
using Json = nlohmann::json;
std::uint64_t integer(const Json& value, std::uint64_t maximum) {
    if (!value.is_number_integer() || (!value.is_number_unsigned() && value.get<std::int64_t>() < 0))
        throw std::invalid_argument("Expected nonnegative integer");
    const auto n = value.get<std::uint64_t>();
    if (n > maximum) throw std::invalid_argument("Integer exceeds supported range");
    return n;
}
void fields(const Json& value, std::initializer_list<std::string_view> allowed) {
    if (!value.is_object()) throw std::invalid_argument("Expected an object");
    for (const auto& [key, item] : value.items()) {
        (void)item;
        if (std::find(allowed.begin(), allowed.end(), key) == allowed.end()) throw std::invalid_argument("Unexpected field: " + key);
    }
}
std::uint32_t label_index(const Json& value) {
    const auto label = value.get<std::string>();
    if (!label.starts_with("card_")) throw std::invalid_argument("Unknown card identity");
    std::uint32_t index; const auto result = std::from_chars(label.data() + 5, label.data() + label.size(), index);
    if (result.ec != std::errc{} || result.ptr != label.data() + label.size() || label != "card_" + std::to_string(index))
        throw std::invalid_argument("Malformed card identity");
    return index;
}
}
void Session::initialize(const Json& setup) {
    fields(setup, {"kind", "seed", "character", "ascension", "clock", "encounter"});
    const auto seed = setup.at("seed").get<std::string>();
    const auto character = setup.at("character").get<std::string>();
    std::string character_id;
    for (const auto& name : {"Ironclad", "Silent", "Defect", "Regent", "Necrobinder"}) if (character == name) {
        character_id = "CHARACTER.";
        for (char c : character) character_id += static_cast<char>(c >= 'a' && c <= 'z' ? c - 'a' + 'A' : c);
    }
    if (character_id.empty()) throw std::invalid_argument("Unsupported character");
    const auto& clock = setup.at("clock"); fields(clock, {"kind", "epoch_seconds", "seconds_per_action"});
    if (clock.at("kind") != "action_clock_v1") throw std::invalid_argument("Unsupported clock input");
    SeededConfig config; config.seed = seed; config.character = character_id;
    config.ascension = static_cast<std::uint8_t>(integer(setup.at("ascension"), 10));
    config.clock.epoch_seconds = static_cast<std::int64_t>(integer(clock.at("epoch_seconds"), RunClock::max_seconds));
    config.clock.seconds_per_action = static_cast<std::int64_t>(integer(clock.at("seconds_per_action"), 86400));
    RunState next; StepResult result{StepStatus::unsupported_content, {}};
    if (setup.at("kind") == "seeded_combat") {
        if (setup.at("encounter") != "SHRINKER_BEETLE_WEAK") throw std::invalid_argument("Unsupported encounter");
        result = initialize_seeded_combat(next, config);
    } else if (setup.at("kind") == "seeded_run") {
        if (setup.contains("encounter")) throw std::invalid_argument("Run setup cannot specify an encounter");
        result = initialize_run(next, config);
    }
    if (result.status != StepStatus::awaiting_input) throw std::invalid_argument("Unsupported simulator setup");
    std::vector<CardHandle> labels;
    if (next.view().combat) for (const auto& zone : next.view().combat->zone_views()) labels.insert(labels.end(), zone.begin(), zone.end());
    state_ = std::move(next); cards_ = std::move(labels); scratch_ = {};
}
StepResult Session::apply(const Json& action) {
    const auto kind = action.at("kind").get<std::string>();
    if (kind == "play_card") {
        fields(action, {"kind", "card_instance", "target"});
        const auto index = label_index(action.at("card_instance"));
        if (index >= cards_.size()) throw std::invalid_argument("Unknown card identity");
        if (action.contains("target") && action.at("target") != "enemy_0") throw std::invalid_argument("Unsupported target");
        return apply_combat(state_, {state_.view().boundary, InputKind::play_card, cards_[index], action.contains("target")}, scratch_);
    }
    if (kind == "select_cards") {
        fields(action, {"kind", "card_instances"});
        const auto& selected = action.at("card_instances");
        if (!selected.is_array() || selected.size() != 1) return {StepStatus::invalid_action, state_.view().boundary, Restriction::invalid_selection};
        const auto index = label_index(selected[0]);
        if (index >= cards_.size()) throw std::invalid_argument("Unknown card identity");
        return apply_combat(state_, {state_.view().boundary, InputKind::select_card, cards_[index]}, scratch_);
    }
    if (kind == "end_turn") {
        fields(action, {"kind"}); return apply_combat(state_, {state_.view().boundary, InputKind::end_turn}, scratch_);
    }
    if (kind == "take_reward" || kind == "reward_alternative") {
        fields(action, {"kind", "index"});
        return apply_run(state_, {state_.view().boundary, kind == "take_reward" ? RunInputKind::take_reward : RunInputKind::reward_alternative,
            static_cast<std::uint32_t>(integer(action.at("index"), UINT32_MAX))});
    }
    if (kind == "select_card_reward") {
        fields(action, {"kind", "card_index"});
        return apply_run(state_, {state_.view().boundary, RunInputKind::choose_reward_card, static_cast<std::uint32_t>(integer(action.at("card_index"), UINT32_MAX))});
    }
    if (kind == "skip_rewards") { fields(action, {"kind"}); return apply_run(state_, {state_.view().boundary, RunInputKind::skip_rewards}); }
    if (kind == "skip_card_reward") { fields(action, {"kind"}); return apply_run(state_, {state_.view().boundary, RunInputKind::reward_alternative, 0}); }
    if (kind == "discard_potion") {
        fields(action, {"kind", "potion_index"});
        return apply_run(state_, {state_.view().boundary, RunInputKind::discard_potion, static_cast<std::uint32_t>(integer(action.at("potion_index"), UINT32_MAX))});
    }
    if (kind == "choose_option") {
        fields(action, {"kind", "option_index"});
        return apply_run(state_, {state_.view().boundary, RunInputKind::choose_option, static_cast<std::uint32_t>(integer(action.at("option_index"), UINT32_MAX))});
    }
    if (kind == "proceed") { fields(action, {"kind"}); return apply_run(state_, {state_.view().boundary, RunInputKind::proceed}); }
    if (kind == "select_map_node") {
        fields(action, {"kind", "col", "row"});
        const auto result = apply_run(state_, {state_.view().boundary, RunInputKind::travel, 0,
            static_cast<int>(integer(action.at("col"), INT32_MAX)), static_cast<int>(integer(action.at("row"), INT32_MAX))});
        if (result.status == StepStatus::awaiting_input && state_.view().combat) {
            cards_.clear();
            for (const auto& zone : state_.view().combat->zone_views()) cards_.insert(cards_.end(), zone.begin(), zone.end());
        }
        return result;
    }
    return {StepStatus::unsupported_content, state_.view().boundary, Restriction::unsupported_setup};
}
nlohmann::json Session::snapshot() const { return exporter_.snapshot(state_); }
} // namespace sts2::engine
