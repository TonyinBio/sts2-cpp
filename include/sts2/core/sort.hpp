#pragma once
#include <algorithm>
#include <bit>
#include <span>

namespace sts2::engine {
// .NET ArraySortHelper introsort ordering, including equal-key swaps. Native
// StableShuffle sorts CardModel by model ID/upgrade before Fisher-Yates; sorting
// equal cards differently changes instance identities even when names match.
template<class T, class Less> void game_sort(std::span<T> values, Less less) noexcept {
    auto swap_if_greater = [&](std::size_t a, std::size_t b) {
        if (a != b && less(values[b], values[a])) std::swap(values[a], values[b]);
    };
    auto intro = [&](auto&& self, std::size_t lo, std::size_t hi, unsigned depth) -> void {
        while (hi > lo) {
            const auto size = hi - lo + 1;
            if (size <= 16) {
                if (size == 2) { swap_if_greater(lo, hi); return; }
                if (size == 3) { swap_if_greater(lo, hi - 1); swap_if_greater(lo, hi); swap_if_greater(hi - 1, hi); return; }
                for (auto i = lo; i < hi; ++i) {
                    auto item = values[i + 1]; auto j = i + 1;
                    while (j > lo && less(item, values[j - 1])) { values[j] = values[j - 1]; --j; }
                    values[j] = item;
                }
                return;
            }
            if (depth == 0) {
                // Native one-based heapsort, not std::sort's unspecified tie order.
                auto down = [&](std::size_t i, std::size_t n) {
                    auto item = values[lo + i - 1];
                    while (i <= n / 2) {
                        auto child = 2 * i;
                        if (child < n && less(values[lo + child - 1], values[lo + child])) ++child;
                        if (!less(item, values[lo + child - 1])) break;
                        values[lo + i - 1] = values[lo + child - 1]; i = child;
                    }
                    values[lo + i - 1] = item;
                };
                for (auto i = size / 2; i > 0; --i) down(i, size);
                for (auto i = size; i > 1; --i) { std::swap(values[lo], values[lo + i - 1]); down(1, i - 1); }
                return;
            }
            --depth;
            auto middle = lo + (hi - lo) / 2;
            swap_if_greater(lo, middle); swap_if_greater(lo, hi); swap_if_greater(middle, hi);
            const auto pivot = values[middle]; std::swap(values[middle], values[hi - 1]);
            auto left = lo, right = hi - 1;
            for (;;) {
                do { ++left; } while (less(values[left], pivot));
                do { --right; } while (less(pivot, values[right]));
                if (left >= right) break;
                std::swap(values[left], values[right]);
            }
            if (left != hi - 1) std::swap(values[left], values[hi - 1]);
            self(self, left + 1, hi, depth);
            hi = left - 1;
        }
    };
    if (values.size() > 1) intro(intro, 0, values.size() - 1, 2 * std::bit_width(values.size()));
}
} // namespace sts2::engine
