#pragma once
#include <compare>
#include <cstdint>
#include <limits>

namespace sts2::engine {
template<class Tag> struct Id {
    std::uint32_t value = 0;
    friend auto operator<=>(const Id&, const Id&) = default;
};
template<class Tag> struct Handle {
    std::uint32_t slot = std::numeric_limits<std::uint32_t>::max();
    std::uint32_t generation = 0;
    [[nodiscard]] bool present() const noexcept { return generation != 0; }
    friend auto operator<=>(const Handle&, const Handle&) = default;
};
using CardHandle = Handle<struct CardInstanceTag>;
using BoundaryId = Id<struct BoundaryTag>;
} // namespace sts2::engine
