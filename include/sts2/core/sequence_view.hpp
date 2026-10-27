#pragma once
#include "sts2/core/inline_storage.hpp"
#include <iterator>
#include <string_view>

namespace sts2::engine {
// A borrowed collection view. It is never part of a byte-copyable state sequence_data.
// Const views never expose mutable elements, even when copied by value.
template<class T, std::size_t N, bool Const = false> class SequenceView {
    using Data = std::conditional_t<Const, const InlineSequence<T, N>, InlineSequence<T, N>>;
    using Owner = std::conditional_t<Const, const OverflowStorage, OverflowStorage>;
public:
    using value_type = T;
    using element_type = std::conditional_t<Const, const T, T>;
    Data& sequence_data;
    Owner& owner;
    SequenceView(Data& sequence, Owner& storage) noexcept : sequence_data(sequence), owner(storage) {}
    SequenceView(const SequenceView&) noexcept = default;
    operator SequenceView<T, N, true>() const noexcept requires (!Const) { return {sequence_data, owner}; }
    [[nodiscard]] auto values() const noexcept { return sequence_data.values(owner); }
    [[nodiscard]] auto begin() const noexcept { return values().begin(); }
    [[nodiscard]] auto end() const noexcept { return values().end(); }
    [[nodiscard]] auto data() const noexcept { return values().data(); }
    [[nodiscard]] auto size() const noexcept { return sequence_data.size(); }
    [[nodiscard]] auto capacity() const noexcept { return sequence_data.capacity(owner); }
    [[nodiscard]] bool empty() const noexcept { return sequence_data.empty(); }
    [[nodiscard]] decltype(auto) operator[](std::size_t i) const noexcept { return values()[i]; }
    [[nodiscard]] decltype(auto) front() const noexcept { return values().front(); }
    [[nodiscard]] decltype(auto) back() const noexcept { return values().back(); }
    operator std::span<element_type>() const noexcept { return values(); }
    operator std::span<const T>() const noexcept requires (!Const) { return values(); }
    void reserve(std::size_t n) requires (!Const) { sequence_data.reserve(owner, n); }
    void push_back(T value) requires (!Const) { sequence_data.push_back(owner, value); }
    void pop_back() noexcept requires (!Const) { sequence_data.pop_back(); }
    void clear() noexcept requires (!Const) { sequence_data.clear(); }
    void resize(std::size_t n) requires (!Const) {
        reserve(n);
        while (sequence_data.count < n) push_back(T{});
        sequence_data.count = static_cast<std::uint32_t>(n);
    }
    auto erase(typename std::span<element_type>::iterator first,
               typename std::span<element_type>::iterator last) noexcept requires (!Const) {
        const auto offset = first - begin();
        const auto removed = last - first;
        std::move(last, end(), first);
        sequence_data.count -= static_cast<std::uint32_t>(removed);
        return begin() + offset;
    }
    auto erase(typename std::span<element_type>::iterator at) noexcept requires (!Const) { return erase(at, at + 1); }
    template<std::forward_iterator Iterator> void assign(Iterator first, Iterator last) requires (!Const) {
        // CombatState transitions use distinct collections. Preserve an aliased input
        // explicitly before a mutation that might invalidate its iterators.
        std::vector<T> alias;
        if constexpr (std::contiguous_iterator<Iterator>) {
            const auto* start = std::to_address(first);
            if (!empty() && std::less_equal<const T*>{}(data(), start) && std::less<const T*>{}(start, data() + size())) {
                alias.assign(first, last); assign(alias.begin(), alias.end()); return;
            }
        }
        const auto n = static_cast<std::size_t>(std::distance(first, last));
        reserve(n); clear();
        for (; first != last; ++first) push_back(*first);
    }
    template<std::forward_iterator Iterator> typename std::span<element_type>::iterator insert(typename std::span<element_type>::iterator position, Iterator first, Iterator last) requires (!Const) {
        const auto offset = position - begin();
        std::vector<T> alias;
        if constexpr (std::contiguous_iterator<Iterator>) {
            const auto* start = std::to_address(first);
            if (!empty() && std::less_equal<const T*>{}(data(), start) && std::less<const T*>{}(start, data() + size())) {
                alias.assign(first, last); return insert(position, alias.begin(), alias.end());
            }
        }
        const auto n = static_cast<std::size_t>(std::distance(first, last));
        const auto old_size = size();
        reserve(old_size + n);
        auto destination = data() + offset;
        std::move_backward(destination, data() + old_size, data() + old_size + n);
        std::copy(first, last, destination);
        sequence_data.count = static_cast<std::uint32_t>(old_size + n);
        return begin() + offset;
    }
    SequenceView& operator=(std::span<const T> other) requires (!Const) { assign(other.begin(), other.end()); return *this; }
    SequenceView& operator=(const SequenceView& other) requires (!Const) {
        if (&data != &other.data || &owner != &other.owner) assign(other.begin(), other.end());
        return *this;
    }
    template<bool Other> bool operator==(const SequenceView<T, N, Other>& other) const { return std::ranges::equal(values(), other.values()); }
    bool operator==(const std::vector<T>& other) const { return std::ranges::equal(values(), other); }
};

template<class T, std::size_t N> SequenceView(InlineSequence<T, N>&, OverflowStorage&) -> SequenceView<T, N, false>;
template<class T, std::size_t N> SequenceView(const InlineSequence<T, N>&, const OverflowStorage&) -> SequenceView<T, N, true>;

template<std::size_t N, bool Const = false> struct TextView {
    SequenceView<char, N, Const> sequence;
    TextView(std::conditional_t<Const, const InlineSequence<char, N>, InlineSequence<char, N>>& data,
             std::conditional_t<Const, const OverflowStorage, OverflowStorage>& owner) noexcept : sequence(data, owner) {}
    TextView(const TextView&) noexcept = default;
    operator TextView<N, true>() const noexcept requires (!Const) { return {sequence.data, sequence.owner}; }
    operator std::string_view() const noexcept { return {sequence.data(), sequence.size()}; }
    auto size() const noexcept { return sequence.size(); }
    bool empty() const noexcept { return sequence.empty(); }
    auto begin() const noexcept { return sequence.begin(); }
    auto end() const noexcept { return sequence.end(); }
    auto data() const noexcept { return sequence.data(); }
    auto capacity() const noexcept { return sequence.capacity(); }
    bool starts_with(std::string_view prefix) const noexcept { return std::string_view(*this).starts_with(prefix); }
    void push_back(char c) requires (!Const) { sequence.push_back(c); }
    void reserve(std::size_t n) requires (!Const) { sequence.reserve(n); }
    TextView& operator=(std::string_view text) requires (!Const) { sequence.assign(text.begin(), text.end()); return *this; }
    TextView& operator=(const TextView& text) requires (!Const) { return *this = std::string_view(text); }
    template<bool Other> bool operator==(const TextView<N, Other>& other) const noexcept { return std::string_view(*this) == std::string_view(other); }
    bool operator==(std::string_view other) const noexcept { return std::string_view(*this) == other; }
};

// Allows optional-data views to support ordinary arrow expressions without
// persisting a view (or a pointer) in the complete-state sequence_data.
template<class View> struct ViewArrow {
    View value;
    View* operator->() noexcept { return &value; }
    const View* operator->() const noexcept { return &value; }
};
} // namespace sts2::engine
