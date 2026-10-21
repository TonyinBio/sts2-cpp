#pragma once
#include <cstdint>

namespace sts2 {
// Shared action_clock_v1 input. Time is constant during an action and advances
// immediately before its effects. Observation/checkpoint calls never advance it.
struct ClockConfig {
    std::int64_t epoch_seconds = 1700000000;
    std::int64_t seconds_per_action = 1;
    friend bool operator==(const ClockConfig&, const ClockConfig&) = default;
};

class RunClock {
public:
    static constexpr std::int64_t max_seconds = 253402300799;

    // Invalid configuration and exhaustion leave the complete clock unchanged.
    [[nodiscard]] bool initialize(ClockConfig config) noexcept {
        if (config.epoch_seconds < 0 || config.epoch_seconds > max_seconds ||
            config.seconds_per_action < 0 || config.seconds_per_action > 86400)
            return false;
        config_ = config;
        elapsed_seconds_ = 0;
        return true;
    }
    [[nodiscard]] bool restore(ClockConfig config, std::int64_t elapsed) noexcept {
        if (config.epoch_seconds < 0 || config.epoch_seconds > max_seconds ||
            config.seconds_per_action < 0 || config.seconds_per_action > 86400 ||
            elapsed < 0 || elapsed > max_seconds - config.epoch_seconds) return false;
        config_ = config;
        elapsed_seconds_ = elapsed;
        return true;
    }
    [[nodiscard]] bool advance_action() noexcept {
        if (config_.seconds_per_action > max_seconds - now_seconds()) return false;
        elapsed_seconds_ += config_.seconds_per_action;
        return true;
    }
    [[nodiscard]] ClockConfig config() const noexcept { return config_; }
    [[nodiscard]] std::int64_t now_seconds() const noexcept {
        return config_.epoch_seconds + elapsed_seconds_;
    }
    [[nodiscard]] std::int64_t elapsed_seconds() const noexcept { return elapsed_seconds_; }
    friend bool operator==(const RunClock&, const RunClock&) = default;
private:
    ClockConfig config_;
    std::int64_t elapsed_seconds_ = 0;
};
} // namespace sts2
