#include "sts2/core/inline_storage.hpp"
#include "storage_allocator.hpp"
#include <gtest/gtest.h>
#include <array>
#include <cstring>

namespace sts2::engine {
namespace {
using test::AllocationCounter;
struct TestRecord {
    InlineSequence<int, 2> deck;
    InlineSequence<int, 1> history;
    std::uint32_t generation = 1;
    template<class Visit> void for_each_sequence(Visit visit) const noexcept { visit(deck); }
};
static_assert(std::is_trivially_copyable_v<TestRecord>);

TEST(InlineStorage, InlineRecordsRelocateAndCloneWithoutAllocating) {
    AllocationCounter allocator;
    {
        InlineState<TestRecord> source(allocator.allocator()), destination(allocator.allocator());
        source.data.deck.push_back(source.overflow, 11);
        source.data.deck.push_back(source.overflow, 22);
        source.data.history.push_back(source.overflow, 33);
        source.data.generation = 7;
        TestRecord relocated;
        std::memcpy(&relocated, &source.data, sizeof(TestRecord));
        EXPECT_EQ(relocated.deck.values(source.overflow)[1], 22);
        clone_into(destination, source);
        EXPECT_EQ(allocator.allocations, 0u);
        EXPECT_EQ(destination.data.generation, 7u);
        destination.data.deck.values(destination.overflow)[0] = 44;
        EXPECT_EQ(source.data.deck.values(source.overflow)[0], 11);
    }
    EXPECT_EQ(allocator.outstanding, 0u);
}

TEST(InlineStorage, OverflowPreservesOrderAndSurvivesShrinkUntilReset) {
    AllocationCounter allocator;
    {
        InlineState<TestRecord> state(allocator.allocator());
        auto& deck = state.data.deck;
        deck.push_back(state.overflow, 11); deck.push_back(state.overflow, 22);
        EXPECT_FALSE(deck.overflowed());
        deck.push_back(state.overflow, 33);
        EXPECT_TRUE(deck.overflowed());
        deck.push_back(state.overflow, 44);
        // The appended value is captured before reserve invalidates its span.
        deck.push_back(state.overflow, deck.values(state.overflow)[0]);
        EXPECT_TRUE(std::ranges::equal(deck.values(state.overflow), std::array{11, 22, 33, 44, 11}));
        const auto id = deck.overflow_id;
        const auto capacity = deck.capacity(state.overflow);
        while (deck.size() > 1) deck.pop_back();
        EXPECT_EQ(deck.overflow_id, id);
        EXPECT_EQ(deck.capacity(state.overflow), capacity);
        deck.clear();
        EXPECT_TRUE(deck.overflowed());
        const auto allocations = allocator.allocations;
        deck.push_back(state.overflow, 55);
        EXPECT_EQ(allocator.allocations, allocations);
        EXPECT_EQ(deck.values(state.overflow)[0], 55);
        state.reset();
        EXPECT_FALSE(state.data.deck.overflowed());
        EXPECT_TRUE(state.overflow.empty());
        state.data.deck.push_back(state.overflow, 66);
        EXPECT_EQ(allocator.allocations, allocations);
    }
    EXPECT_EQ(allocator.outstanding, 0u);
}

TEST(InlineStorage, OverflowClonesOwnAllCollectionsIndependently) {
    AllocationCounter allocator;
    {
        InlineState<TestRecord> source(allocator.allocator()), destination(allocator.allocator());
        for (int value : {11, 22, 33}) source.data.deck.push_back(source.overflow, value);
        // A second overflow-bearing history is copied without a clone-field list.
        for (int value : {44, 55}) source.data.history.push_back(source.overflow, value);
        clone_into(destination, source);
        EXPECT_TRUE(source.data.deck.equals(source.overflow, destination.data.deck, destination.overflow));
        EXPECT_TRUE(source.data.history.equals(source.overflow, destination.data.history, destination.overflow));
        destination.data.deck.values(destination.overflow)[0] = 66;
        destination.data.history.values(destination.overflow)[0] = 77;
        EXPECT_EQ(source.data.deck.values(source.overflow)[0], 11);
        EXPECT_EQ(source.data.history.values(source.overflow)[0], 44);
        source.data.deck.push_back(source.overflow, 88);
        EXPECT_EQ(destination.data.deck.size(), 3u);
        // Reusing a previously overflowed destination replaces both records and
        // storage. Self-cloning preserves every owned collection.
        clone_into(destination, source);
        clone_into(destination, destination);
        EXPECT_TRUE(source.data.deck.equals(source.overflow, destination.data.deck, destination.overflow));
        InlineState<TestRecord> common(allocator.allocator());
        common.data.deck.push_back(common.overflow, 99);
        const auto allocations = allocator.allocations;
        clone_into(destination, common);
        EXPECT_EQ(allocator.allocations, allocations);
        EXPECT_TRUE(destination.overflow.empty());
        EXPECT_FALSE(destination.data.deck.overflowed());
        EXPECT_EQ(destination.data.deck.values(destination.overflow)[0], 99);
    }
    EXPECT_EQ(allocator.outstanding, 0u);
}

TEST(InlineStorage, SemanticEqualityIgnoresStorageModeAndCapacity) {
    InlineState<TestRecord> inline_state, overflow_state;
    for (int value : {11, 22}) {
        inline_state.data.deck.push_back(inline_state.overflow, value);
        overflow_state.data.deck.push_back(overflow_state.overflow, value);
    }
    overflow_state.data.deck.reserve(overflow_state.overflow, 200);
    EXPECT_FALSE(inline_state.data.deck.overflowed());
    EXPECT_TRUE(overflow_state.data.deck.overflowed());
    EXPECT_TRUE(inline_state.data.deck.equals(inline_state.overflow, overflow_state.data.deck, overflow_state.overflow));
}

TEST(InlineStorage, AllocationFailuresPreserveRecordsAndReleaseTemporaryOwnership) {
    // Exercise failure on the block table, each independently owned block, and
    // the successful boundary after all allocations have been made.
    for (std::size_t allowed = 0; allowed <= 3; ++allowed) {
        AllocationCounter allocator;
        {
            InlineState<TestRecord> source, destination(allocator.allocator());
            for (int value : {11, 22, 33}) source.data.deck.push_back(source.overflow, value);
            for (int value : {44, 55}) source.data.history.push_back(source.overflow, value);
            for (int value : {66, 77, 88}) destination.data.deck.push_back(destination.overflow, value);
            destination.data.generation = 9;
            const auto outstanding = allocator.outstanding;
            allocator.fail_after = allocator.allocations + allowed;
            if (allowed < 3) {
                EXPECT_THROW(clone_into(destination, source), std::bad_alloc);
                EXPECT_EQ(destination.data.generation, 9u);
                EXPECT_EQ(destination.data.deck.values(destination.overflow)[0], 66);
                EXPECT_EQ(destination.data.deck.size(), 3u);
                EXPECT_EQ(allocator.outstanding, outstanding);
            } else {
                EXPECT_NO_THROW(clone_into(destination, source));
                EXPECT_EQ(destination.data.generation, 1u);
                EXPECT_EQ(destination.data.history.values(destination.overflow)[1], 55);
            }
        }
        EXPECT_EQ(allocator.outstanding, 0u);
    }
}

TEST(InlineStorage, FailedPromotionAndGrowthPreserveValuesAndOwnership) {
    for (bool grow : {false, true}) {
        AllocationCounter allocator;
        {
            InlineState<TestRecord> state(allocator.allocator());
            auto& deck = state.data.deck;
            for (int value : {11, 22}) deck.push_back(state.overflow, value);
            if (grow) { deck.push_back(state.overflow, 33); deck.push_back(state.overflow, 44); }
            const auto size = deck.size(), capacity = deck.capacity(state.overflow);
            const auto outstanding = allocator.outstanding;
            allocator.fail_after = allocator.allocations;
            EXPECT_THROW(deck.push_back(state.overflow, 55), std::bad_alloc);
            EXPECT_EQ(deck.size(), size);
            EXPECT_EQ(deck.capacity(state.overflow), capacity);
            EXPECT_EQ(deck.overflowed(), grow);
            EXPECT_EQ(deck.values(state.overflow)[0], 11);
            EXPECT_EQ(deck.values(state.overflow)[1], 22);
            EXPECT_EQ(allocator.outstanding, outstanding);
        }
        EXPECT_EQ(allocator.outstanding, 0u);
    }
}

TEST(InlineStorage, AddedOverflowHistoryIsCopiedEvenIfValidationRegistrationIsMissing) {
    InlineState<TestRecord> source, copy;
    source.data.history.push_back(source.overflow, 44);
    source.data.history.push_back(source.overflow, 55);
    EXPECT_FALSE(valid_data_storage(source.data, source.overflow));
    clone_into(copy, source);
    EXPECT_EQ(copy.data.history.values(copy.overflow)[1], 55);
    copy.data.history.values(copy.overflow)[0] = 66;
    EXPECT_EQ(source.data.history.values(source.overflow)[0], 44);
}
TEST(InlineStorage, LargeDeckUsesOverflowWithoutAnInlineCapacityLimit) {
    InlineState<TestRecord> state, destination;
    for (int i = 0; i < 200; ++i) state.data.deck.push_back(state.overflow, i);
    clone_into(destination, state);
    EXPECT_EQ(destination.data.deck.size(), 200u);
    EXPECT_EQ(destination.data.deck.values(destination.overflow)[199], 199);
    EXPECT_TRUE(state.data.deck.equals(state.overflow, destination.data.deck, destination.overflow));
    const auto id = state.data.deck.overflow_id;
    EXPECT_THROW(state.data.deck.reserve(state.overflow, std::size_t(UINT32_MAX) + 1), std::length_error);
    EXPECT_EQ(state.data.deck.overflow_id, id);
    EXPECT_EQ(state.data.deck.size(), 200u);
}
} // namespace
} // namespace sts2::engine
