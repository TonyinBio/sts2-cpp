#pragma once
#include "sts2/json_io/native_run.hpp"
#include "sts2/engine/run.hpp"
#include <utility>

namespace sts2::engine {
// Initialize from setup only, never from an expected comparison snapshot.
void initialize_json_run(RunState&, const nlohmann::json&, CombatTrace = {}, std::span<const CardSpec> = {}, std::span<const ContentId> = {}, std::int32_t enemy_hp = 0, std::uint32_t joss_counter = 0);
// Canonical wire identities are bound from simulator output, never a reference
// snapshot. JSON parsing and allocation stay outside the gameplay kernel.
class Session {
public:
    Session() = default;
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
    Session(Session&& other) noexcept
        : state_(std::move(other.state_)), scratch_(other.scratch_), cards_(std::move(other.cards_)), exporter_(std::move(other.exporter_)) {
        scratch_.trace = exporter_.trace();
    }
    Session& operator=(Session&& other) noexcept {
        if (this != &other) {
            state_ = std::move(other.state_); scratch_ = other.scratch_;
            cards_ = std::move(other.cards_); exporter_ = std::move(other.exporter_);
            scratch_.trace = exporter_.trace();
        }
        return *this;
    }
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
