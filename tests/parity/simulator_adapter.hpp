#pragma once
#include "snapshot.hpp"
#include "sts2/simulator.hpp"
#include <array>
#include <string>

namespace sts2::parity {
inline const std::set<std::string> simulator_features = {"basic_combat", "ironclad_starter_shrinker_v1"};
struct ImportedCombat {
    CombatState state;
    // Immutable streams for this profile stay outside hot branch copies.
    std::array<Rng, rng_count> initial_rngs;
    std::array<std::string, max_cards> card_ids;
};
// The first supported importer accepts only a fresh reference combat, before actions.
// It rejects unsupported models/fields rather than guessing omitted state.
ImportedCombat import_initial(const Snapshot& snapshot);
Snapshot export_snapshot(const ImportedCombat& combat);
Action resolve_action(const ImportedCombat& combat, const nlohmann::json& action);
// Runs from initial once; expected intermediate states are never fed into the engine.
// Throws with the first divergent action and JSON Pointer field.
void replay_and_compare(const nlohmann::json& actions, const nlohmann::json& reference);
}
