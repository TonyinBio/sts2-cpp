#pragma once
#include "sts2/integration/native_run.hpp"
#include "sts2/engine/run.hpp"

namespace sts2::engine {
// Canonical wire identities are bound from simulator output, never a reference
// snapshot. JSON parsing and allocation stay outside the gameplay kernel.
class Session {
public:
    void initialize(const nlohmann::json& setup);
    [[nodiscard]] StepResult apply(const nlohmann::json& action);
    [[nodiscard]] nlohmann::json snapshot() const;
    [[nodiscard]] const RunState& state() const noexcept { return state_; }
private:
    RunState state_;
    Scratch scratch_;
    std::vector<CardHandle> cards_;
    NativeRunExporter exporter_;
};
} // namespace sts2::engine
