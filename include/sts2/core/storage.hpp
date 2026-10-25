#pragma once
#include "sts2/core/ids.hpp"
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>
#include <type_traits>

namespace sts2::engine {
// Contiguous owning pool. Handles survive growth and zone movement; erased
// generations cannot alias replacement objects. Copying owns independent storage.
// Growth is permitted at checked capacity boundaries, never silently truncates.
template<class T, class Tag> class Pool {
public:
    static_assert(std::is_nothrow_copy_assignable_v<T>);
    Pool() = default;
    Pool(const Pool& other) : slots_(other.slots_), free_(other.free_) { free_.reserve(slots_.size()); }
    Pool(Pool&&) noexcept = default;
    Pool& operator=(const Pool& other) {
        if (this != &other) { slots_ = other.slots_; free_ = other.free_; free_.reserve(slots_.size()); }
        return *this;
    }
    Pool& operator=(Pool&&) noexcept = default;
    struct Slot {
        T value{};
        std::uint32_t generation = 1;
        bool live = false;
        friend bool operator==(const Slot&, const Slot&) = default;
    };
    using HandleType = Handle<Tag>;
    void reserve(std::size_t count) { slots_.reserve(count); free_.reserve(count); }
    [[nodiscard]] HandleType create(const T& value, std::size_t limit, std::uint32_t generation = 1) {
        if (generation == 0) return {};
        std::uint32_t index;
        if (!free_.empty()) { index = free_.back(); free_.pop_back(); }
        else {
            if (slots_.size() >= limit || slots_.size() >= std::numeric_limits<std::uint32_t>::max()) return {};
            index = static_cast<std::uint32_t>(slots_.size());
            // Keep free-list capacity sufficient for allocation-free deletion.
            if (free_.capacity() < slots_.size() + 1) free_.reserve(slots_.size() + 1);
            slots_.push_back({{}, generation, false});
        }
        auto& slot = slots_[index]; slot.value = value; slot.live = true;
        return {index, slot.generation};
    }
    [[nodiscard]] T* get(HandleType handle) noexcept {
        if (handle.slot >= slots_.size()) return nullptr;
        auto& slot = slots_[handle.slot];
        return slot.live && slot.generation == handle.generation ? &slot.value : nullptr;
    }
    [[nodiscard]] const T* get(HandleType handle) const noexcept {
        if (handle.slot >= slots_.size()) return nullptr;
        const auto& slot = slots_[handle.slot];
        return slot.live && slot.generation == handle.generation ? &slot.value : nullptr;
    }
    [[nodiscard]] bool erase(HandleType handle) noexcept {
        if (!get(handle)) return false;
        auto& slot = slots_[handle.slot]; slot.live = false;
        if (slot.generation != std::numeric_limits<std::uint32_t>::max()) {
            ++slot.generation; free_.push_back(handle.slot);
        } // Exhausted generation retires this slot forever.
        return true;
    }
    [[nodiscard]] bool restore(std::vector<Slot> slots, std::vector<std::uint32_t> free) {
        for (std::size_t i = 0; i < slots.size(); ++i) {
            if (slots[i].generation == 0) return false;
            std::size_t occurrences = 0;
            for (auto index : free) if (index == i) ++occurrences;
            const bool reusable = !slots[i].live && slots[i].generation != std::numeric_limits<std::uint32_t>::max();
            if (occurrences != (reusable ? 1U : 0U)) return false;
        }
        for (auto index : free) if (index >= slots.size()) return false;
        free.reserve(slots.size());
        slots_ = std::move(slots); free_ = std::move(free);
        return true;
    }
    [[nodiscard]] std::span<const Slot> slots() const noexcept { return slots_; }
    [[nodiscard]] std::span<const std::uint32_t> free_slots() const noexcept { return free_; }
    [[nodiscard]] std::size_t retained_bytes() const noexcept {
        return slots_.capacity() * sizeof(Slot) + free_.capacity() * sizeof(std::uint32_t);
    }
    friend bool operator==(const Pool&, const Pool&) = default;
private:
    std::vector<Slot> slots_;
    std::vector<std::uint32_t> free_;
};
} // namespace sts2::engine
