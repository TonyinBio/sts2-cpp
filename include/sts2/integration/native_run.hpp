#pragma once
#include "sts2/state/run.hpp"
#include <nlohmann/json.hpp>

namespace sts2::engine {
// Privileged versioned gameplay comparison export, not a public observation. Immutable
// canonical model defaults come from the audited rules, never expected states.
class NativeRunExporter {
public:
    NativeRunExporter();
    [[nodiscard]] nlohmann::json snapshot(const RunState&) const;
private:
    const nlohmann::json defaults_;
};
} // namespace sts2::engine
