#pragma once
#include "sts2/state/run.hpp"
#include <nlohmann/json.hpp>
#include <vector>

namespace sts2::engine {
// Privileged versioned gameplay comparison export, not a public observation. Immutable
// canonical model defaults come from the audited rules, never expected states.
// Diagnostic ownership lives here, never in CombatData or a simulation clone.
struct RecordedCardPlay {
    CardHandle card;
    std::int32_t energy = 0;
    bool enemy_target = false;
};
struct RecordedCombat {
    bool attached = false;
    BoundaryId boundary {};
    std::uint32_t card_generation = 0;
    std::vector<CombatEvent> history;
    std::vector<RecordedCardPlay> plays;
    std::vector<PowerInstance> powers;
};
class NativeRunExporter {
public:
    NativeRunExporter();
    NativeRunExporter(NativeRunExporter&&) = default;
    NativeRunExporter& operator=(NativeRunExporter&&) = default;
    NativeRunExporter(const NativeRunExporter&) = delete;
    NativeRunExporter& operator=(const NativeRunExporter&) = delete;
    // Supply at creation and on every transition of this linear execution.
    [[nodiscard]] CombatTrace trace() noexcept;
    void reset() noexcept { combat_ = {}; }

    [[nodiscard]] nlohmann::json snapshot(const RunState&) const;
private:
    void record(ConstCombatView, const CombatEvent&);
    RecordedCombat combat_;
    nlohmann::json defaults_;
};
} // namespace sts2::engine
