#pragma once
#include "sts2/engine/run.hpp"

namespace sts2::engine::detail {
// Internal mechanic: upgrade a combat card and its persistent origin.
[[nodiscard]] Restriction upgrade_card_permanently(RunState&, CardHandle) noexcept;
} // namespace sts2::engine::detail
