#pragma once
#include "sts2/core/ids.hpp"
#include "sts2/core/sequence_view.hpp"

namespace sts2::engine {
template<class T, class Tag, std::size_t N = 32> struct PoolRecord {
    static_assert(std::is_trivially_copyable_v<T>);
    struct Slot {
        T value{};
        std::uint32_t generation = 1;
        bool live = false;
        friend bool operator==(const Slot&, const Slot&) = default;
    };
    std::uint32_t highest_generation = 0;
    InlineSequence<Slot, N> slots;
    InlineSequence<std::uint32_t, N> free;
};

template<class T, class Tag, std::size_t N = 32, bool Const = false> struct PoolView {
    using Record = PoolRecord<T, Tag, N>;
    using Slot = typename Record::Slot;
    using HandleType = Handle<Tag>;
    std::conditional_t<Const, const Record, Record>& record;
    std::conditional_t<Const, const OverflowStorage, OverflowStorage>& owner;
    operator PoolView<T, Tag, N, true>() const noexcept requires (!Const) { return {record, owner}; }
    void reserve(std::size_t count) requires (!Const) { record.slots.reserve(owner, count); record.free.reserve(owner, count); }
    [[nodiscard]] HandleType create(T value, std::size_t limit, std::uint32_t generation = 1) requires (!Const) {
        if (generation == 0) return {};
        auto free = SequenceView(record.free, owner);
        auto slots = SequenceView(record.slots, owner);
        std::uint32_t index;
        if (!free.empty()) { index = free.back(); free.pop_back(); }
        else {
            if (slots.size() >= limit || slots.size() >= UINT32_MAX) return {};
            index = static_cast<std::uint32_t>(slots.size());
            // Deletion must not allocate, even after slot storage grows.
            free.reserve(slots.size() + 1);
            slots.push_back({{}, generation, false});
        }
        auto& slot = slots[index]; slot.value = value; slot.live = true;
        record.highest_generation = std::max(record.highest_generation, slot.generation);
        return {index, slot.generation};
    }
    [[nodiscard]] auto get(HandleType handle) const noexcept -> std::conditional_t<Const, const T*, T*> {
        auto slots = record.slots.values(owner);
        if (handle.slot >= slots.size()) return nullptr;
        auto& slot = slots[handle.slot];
        return slot.live && slot.generation == handle.generation ? &slot.value : nullptr;
    }
    [[nodiscard]] bool erase(HandleType handle) noexcept requires (!Const) {
        if (!get(handle)) return false;
        auto& slot = record.slots.values(owner)[handle.slot]; slot.live = false;
        if (slot.generation != UINT32_MAX) { ++slot.generation; record.highest_generation = std::max(record.highest_generation, slot.generation); record.free.push_back(owner, handle.slot); }
        return true;
    }
    [[nodiscard]] bool restore(std::vector<Slot> slots, std::vector<std::uint32_t> free) requires (!Const) {
        for (std::size_t i = 0; i < slots.size(); ++i) {
            if (slots[i].generation == 0) return false;
            std::size_t occurrences = 0;
            for (auto index : free) if (index == i) ++occurrences;
            const bool reusable = !slots[i].live && slots[i].generation != UINT32_MAX;
            if (occurrences != (reusable ? 1U : 0U)) return false;
        }
        for (auto index : free) if (index >= slots.size()) return false;
        std::uint32_t highest = 0;
        for (const auto& slot : slots) highest = std::max(highest, slot.generation);
        reserve(slots.size());
        SequenceView(record.slots, owner).assign(slots.begin(), slots.end());
        SequenceView(record.free, owner).assign(free.begin(), free.end());
        record.highest_generation = highest;
        return true;
    }
    [[nodiscard]] auto slots() const noexcept { return record.slots.values(owner); }
    [[nodiscard]] auto free_slots() const noexcept { return record.free.values(owner); }
    [[nodiscard]] bool valid() const noexcept {
        if (record.free.capacity(owner) < record.slots.size()) return false;
        for (auto index : free_slots()) if (index >= slots().size()) return false;
        for (std::size_t i = 0; i < slots().size(); ++i) {
            const auto& slot = slots()[i];
            if (slot.generation == 0 || slot.generation > record.highest_generation) return false;
            std::size_t occurrences = 0;
            for (auto index : free_slots()) if (index == i) ++occurrences;
            const bool reusable = !slot.live && slot.generation != UINT32_MAX;
            if (occurrences != (reusable ? 1U : 0U)) return false;
        }
        return true;
    }
    template<bool Other> bool operator==(const PoolView<T, Tag, N, Other>& other) const {
        return record.highest_generation == other.record.highest_generation && std::ranges::equal(slots(), other.slots()) && std::ranges::equal(free_slots(), other.free_slots());
    }
};
} // namespace sts2::engine
