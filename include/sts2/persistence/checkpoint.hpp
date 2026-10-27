#pragma once
#include "sts2/state/combat.hpp"
#include "sts2/state/run.hpp"
#include <cstddef>
#include <span>
#include <vector>

namespace sts2::engine {
struct Checkpoint { std::vector<std::byte> bytes; };
enum class RestoreStatus { restored, incompatible, malformed, resource_limit };
// Explicit little-endian, versioned, checksummed, complete owning state. Format
// is independent of native C++ padding, pointers, allocator state, and endianness.
[[nodiscard]] Checkpoint checkpoint(ConstCombatView);
// Decode and validate a temporary state; publish only on full success.
[[nodiscard]] RestoreStatus restore(CombatView, std::span<const std::byte>, Scratch&, Limits = {}) noexcept;
[[nodiscard]] std::uint64_t content_fingerprint();
[[nodiscard]] Checkpoint checkpoint(const RunState&);
[[nodiscard]] RestoreStatus restore(RunState&, std::span<const std::byte>, Scratch&, Limits = {}) noexcept;
} // namespace sts2::engine
