#pragma once
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <new>
#include <span>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include <vector>

namespace sts2::engine {
inline constexpr std::uint32_t no_overflow = UINT32_MAX;
enum class CloneStatus : std::uint8_t { cloned, resource_limit };

// Allocation is an operational boundary, not a polymorphic state object. The
// optional context must outlive the owner. These callbacks also let tests inject
// failure without changing the representation or the production clone path.
// allocate may throw std::bad_alloc; deallocate must not throw.
struct StorageAllocator {
    void* context = nullptr;
    void* (*allocate)(void*, std::size_t, std::size_t) = [](void*, std::size_t bytes, std::size_t alignment) -> void* {
        if (alignment > __STDCPP_DEFAULT_NEW_ALIGNMENT__) return ::operator new(bytes, std::align_val_t(alignment));
        return ::operator new(bytes);
    };
    void (*deallocate)(void*, void*, std::size_t, std::size_t) = [](void*, void* data, std::size_t, std::size_t alignment) {
        if (alignment > __STDCPP_DEFAULT_NEW_ALIGNMENT__) ::operator delete(data, std::align_val_t(alignment));
        else ::operator delete(data);
    };
    friend bool operator==(const StorageAllocator&, const StorageAllocator&) = default;
};

// Standard allocator adapter for the owner's block-index table. Its allocations
// use the same failure boundary as the separately allocated collection blocks.
template<class T> struct OverflowAllocator {
    using value_type = T;
    using propagate_on_container_move_assignment = std::true_type;
    using propagate_on_container_swap = std::true_type;
    StorageAllocator storage;
    OverflowAllocator(StorageAllocator allocator = {}) noexcept : storage(allocator) {}
    template<class U> OverflowAllocator(const OverflowAllocator<U>& other) noexcept : storage(other.storage) {}
    [[nodiscard]] T* allocate(std::size_t count) {
        if (count > std::numeric_limits<std::size_t>::max() / sizeof(T)) throw std::bad_array_new_length{};
        return static_cast<T*>(storage.allocate(storage.context, count * sizeof(T), alignof(T)));
    }
    void deallocate(T* data, std::size_t count) noexcept {
        storage.deallocate(storage.context, data, count * sizeof(T), alignof(T));
    }
    template<class U> bool operator==(const OverflowAllocator<U>& other) const noexcept { return storage == other.storage; }
};

// Owns individually allocated collections, not an arena. IDs resolve against the
// owning state; neither data structures nor their elements contain pointers into a state.
// Production allocates only on overflow. The owner also contains the block table.
class OverflowStorage {
public:
    explicit OverflowStorage(StorageAllocator allocator = {})
        : blocks_(OverflowAllocator<Block>(allocator)) {}
    OverflowStorage(const OverflowStorage&) = delete;
    OverflowStorage& operator=(const OverflowStorage&) = delete;
    OverflowStorage(OverflowStorage&&) noexcept = default;
    OverflowStorage& operator=(OverflowStorage&&) noexcept = default;
    void swap(OverflowStorage& other) noexcept { blocks_.swap(other.blocks_); }

    [[nodiscard]] bool empty() const noexcept { return blocks_.empty(); }
    [[nodiscard]] std::size_t block_count() const noexcept { return blocks_.size(); }
    template<class T> [[nodiscard]] bool contains(std::uint32_t id, std::size_t count) const noexcept {
        return id < blocks_.size() && blocks_[id].alignment == alignof(T) &&
            blocks_[id].bytes % sizeof(T) == 0 && count <= blocks_[id].bytes / sizeof(T);
    }
    [[nodiscard]] StorageAllocator allocator() const noexcept { return blocks_.get_allocator().storage; }
    [[nodiscard]] std::size_t retained_bytes() const noexcept {
        std::size_t bytes = blocks_.capacity() * sizeof(Block);
        for (const auto& block : blocks_) bytes += block.bytes;
        return bytes;
    }
    void reset() noexcept { blocks_.clear(); }

    // Publication is atomic: a failed copy leaves destination IDs and ownership
    // intact. Every block is copied, including newly introduced history storage.
    void clone_from(const OverflowStorage& source) {
        if (this == &source) return;
        OverflowStorage next(allocator());
        if (!source.empty()) next.blocks_.reserve(source.blocks_.size());
        for (const auto& block : source.blocks_) {
            Block copy(allocator(), block.bytes, block.alignment);
            std::memcpy(copy.data, block.data, block.bytes);
            next.blocks_.push_back(std::move(copy));
        }
        blocks_.swap(next.blocks_);
    }

    template<class T> [[nodiscard]] T* data(std::uint32_t id) noexcept {
        return static_cast<T*>(blocks_[id].data);
    }
    template<class T> [[nodiscard]] const T* data(std::uint32_t id) const noexcept {
        return static_cast<const T*>(blocks_[id].data);
    }
    template<class T> [[nodiscard]] std::size_t capacity(std::uint32_t id) const noexcept {
        return blocks_[id].bytes / sizeof(T);
    }
    template<class T> [[nodiscard]] std::uint32_t create(std::span<const T> values, std::size_t capacity) {
        check_capacity<T>(capacity);
        if (capacity < values.size() || blocks_.size() >= no_overflow) throw std::length_error("Overflow identity exhausted");
        Block block(allocator(), capacity * sizeof(T), alignof(T));
        if (!values.empty()) std::memcpy(block.data, values.data(), values.size_bytes());
        const auto id = static_cast<std::uint32_t>(blocks_.size());
        blocks_.push_back(std::move(block));
        return id;
    }
    template<class T> void grow(std::uint32_t id, std::size_t count, std::size_t capacity) {
        check_capacity<T>(capacity);
        auto& current = blocks_[id];
        if (count > current.bytes / sizeof(T) || capacity < count) throw std::length_error("Invalid overflow growth");
        Block next(allocator(), capacity * sizeof(T), alignof(T));
        if (count) std::memcpy(next.data, current.data, count * sizeof(T));
        current = std::move(next);
    }
private:
    struct Block {
        StorageAllocator allocator;
        void* data;
        std::size_t bytes, alignment;
        Block(StorageAllocator allocation, std::size_t size, std::size_t align)
            : allocator(allocation), data(allocation.allocate(allocation.context, size, align)), bytes(size), alignment(align) {
            // Define unused bytes as well, so copying retained capacity never
            // reads indeterminate values through a typed expression.
            std::memset(data, 0, bytes);
        }
        Block(const Block&) = delete;
        Block& operator=(const Block&) = delete;
        Block(Block&& other) noexcept
            : allocator(other.allocator), data(std::exchange(other.data, nullptr)), bytes(other.bytes), alignment(other.alignment) {}
        Block& operator=(Block&& other) noexcept {
            if (this != &other) {
                release(); allocator = other.allocator; data = std::exchange(other.data, nullptr);
                bytes = other.bytes; alignment = other.alignment;
            }
            return *this;
        }
        ~Block() { release(); }
        void release() noexcept { if (data) allocator.deallocate(allocator.context, data, bytes, alignment); }
    };
    template<class T> static void check_capacity(std::size_t capacity) {
        static_assert(std::is_trivially_copyable_v<T>);
        if (capacity > UINT32_MAX || capacity > std::numeric_limits<std::size_t>::max() / sizeof(T))
            throw std::length_error("Overflow capacity exhausted");
    }
    std::vector<Block, OverflowAllocator<Block>> blocks_;
};

// This is the byte-copyable data. Access requires its complete state's owner,
// making relocation independent of addresses and inline/overflow storage mode.
// Borrowed spans last until growth/reset/destruction of the owning collection.
template<class T, std::size_t InlineCapacity> struct InlineSequence {
    static_assert(std::is_trivially_copyable_v<T>);
    static_assert(InlineCapacity > 0 && InlineCapacity <= UINT32_MAX);
    std::array<T, InlineCapacity> inline_values{};
    std::uint32_t count = 0;
    std::uint32_t overflow_id = no_overflow;

    [[nodiscard]] bool overflowed() const noexcept { return overflow_id != no_overflow; }
    [[nodiscard]] std::size_t size() const noexcept { return count; }
    [[nodiscard]] bool empty() const noexcept { return count == 0; }
    [[nodiscard]] bool valid(const OverflowStorage& owner) const noexcept {
        return overflowed() ? owner.contains<T>(overflow_id, count) : count <= InlineCapacity;
    }
    [[nodiscard]] std::span<T> values(OverflowStorage& owner) noexcept {
        return {overflowed() ? owner.data<T>(overflow_id) : inline_values.data(), count};
    }
    [[nodiscard]] std::span<const T> values(const OverflowStorage& owner) const noexcept {
        return {overflowed() ? owner.data<T>(overflow_id) : inline_values.data(), count};
    }
    [[nodiscard]] std::size_t capacity(const OverflowStorage& owner) const noexcept {
        return overflowed() ? owner.capacity<T>(overflow_id) : InlineCapacity;
    }
    void reserve(OverflowStorage& owner, std::size_t requested) {
        const auto current = capacity(owner);
        if (requested <= current) return;
        if (requested > UINT32_MAX) throw std::length_error("Collection identity exhausted");
        const auto grown = std::max(requested, current > UINT32_MAX / 2 ? std::size_t(UINT32_MAX) : current * 2);
        if (overflowed()) owner.grow<T>(overflow_id, count, grown);
        else overflow_id = owner.create<T>(values(owner), grown);
    }
    void push_back(OverflowStorage& owner, T value) {
        // A value may alias this collection; capture it before reserve relocates.
        reserve(owner, std::size_t(count) + 1);
        auto* data = overflowed() ? owner.data<T>(overflow_id) : inline_values.data();
        data[count++] = value;
    }
    void pop_back() noexcept { if (count) --count; }
    void clear() noexcept { count = 0; } // Deliberately retains overflow.
    // Reset all data and owner together with InlineState::reset(). Resetting
    // an individual sequence cannot reclaim its ID while other data uses it.
    [[nodiscard]] bool equals(const OverflowStorage& owner, const InlineSequence& other,
                              const OverflowStorage& other_owner) const {
        return std::ranges::equal(values(owner), other.values(other_owner));
    }
};

// The data enumerates its authoritative sequences for storage validation.
// Ownership cloning does not use that list: it copies every owned block. An
// omitted new overflow-bearing history therefore fails validation rather than
// silently qualifying for a fast clone with missing ownership.
template<class Data> bool valid_data_storage(const Data& data, const OverflowStorage& owner) noexcept {
    bool valid = true;
    std::size_t overflow_count = 0;
    const auto visit = [&](auto callback) {
        if constexpr (requires { data.for_each_sequence(owner, callback); }) data.for_each_sequence(owner, callback);
        else data.for_each_sequence(callback);
    };
    visit([&](const auto& sequence) {
        if (!sequence.valid(owner)) valid = false;
        if (!sequence.overflowed()) return;
        ++overflow_count;
        std::size_t references = 0;
        visit([&](const auto& other) {
            if (other.overflowed() && other.overflow_id == sequence.overflow_id) ++references;
        });
        if (references != 1) valid = false;
    });
    return valid && overflow_count == owner.block_count();
}

// The data contains all authoritative scalars and inline collections; optional
// heap ownership stays outside its memcpy image. Persistence must encode semantic
// fields explicitly rather than writing this data's bytes.
template<class Data> struct InlineState {
    static_assert(std::is_trivially_copyable_v<Data>);
    static_assert(std::is_nothrow_default_constructible_v<Data>);
    static_assert(std::is_trivially_copy_assignable_v<Data>);
    Data data{};
    OverflowStorage overflow;
    explicit InlineState(StorageAllocator allocator = {}) : overflow(allocator) {}
    void reset() noexcept {
        data = Data{};
        overflow.reset();
    }
};

template<class Data> void clone_into(InlineState<Data>& destination, const InlineState<Data>& source) {
    if (&destination == &source) return;
    // No field-by-field eligibility predicate: every authoritative overflow
    // collection is owned here, so adding a history cannot omit its slow path.
    destination.overflow.clone_from(source.overflow);
    std::memcpy(&destination.data, &source.data, sizeof(Data));
}
} // namespace sts2::engine
