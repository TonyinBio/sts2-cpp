#pragma once
#include "sts2/core/inline_storage.hpp"

namespace sts2::engine::test {
struct AllocationCounter {
    std::size_t allocations = 0;
    std::size_t outstanding = 0;
    std::size_t fail_after = SIZE_MAX;
    StorageAllocator allocator() noexcept {
        return {this,
            [](void* context, std::size_t size, std::size_t alignment) -> void* {
                auto& counter = *static_cast<AllocationCounter*>(context);
                if (counter.allocations >= counter.fail_after) throw std::bad_alloc{};
                auto* result = StorageAllocator{}.allocate(nullptr, size, alignment);
                ++counter.allocations; ++counter.outstanding;
                return result;
            },
            [](void* context, void* pointer, std::size_t size, std::size_t alignment) {
                --static_cast<AllocationCounter*>(context)->outstanding;
                StorageAllocator{}.deallocate(nullptr, pointer, size, alignment);
            }};
    }
};
} // namespace sts2::engine::test
