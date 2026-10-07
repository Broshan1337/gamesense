#include <gtest/gtest.h>

#include <array>
#include <cstring>

#include <GameClient/InputHistory.h>







namespace {

struct Entry {
    std::uint32_t hasBits{};
    std::uint32_t pad{};
};

struct FakeCmd {
    alignas(8) std::byte bytes[256]{};
    // Include a deliberately over-capacity slot for malformed-history tests.
    static constexpr int kSlots = cs2::CUserCmd::kMaxInputHistoryEntries + 1;
    alignas(8) std::byte rep[8 + kSlots * sizeof(void*)]{};
    std::array<Entry, kSlots> entries{};

    [[nodiscard]] Entry* entry(std::size_t index) noexcept { return &entries[index]; }
    [[nodiscard]] void** slot(std::size_t index) noexcept
    {
        return reinterpret_cast<void**>(rep + 8 + static_cast<std::ptrdiff_t>(index) * sizeof(void*));
    }
};

auto asCmd(FakeCmd& cmd) noexcept
{
    return reinterpret_cast<cs2::CUserCmd*>(cmd.bytes);
}

void writeCounts(FakeCmd& cmd, int current, int total)
{
    static_assert(cs2::CUserCmd::InputHistory::kCurrentSizeOffset == 48);
    static_assert(cs2::CUserCmd::InputHistory::kTotalSizeOffset == 52);
    static_assert(cs2::CUserCmd::InputHistory::kRepOffset == 56);
    static_assert(cs2::CUserCmd::InputHistory::kFieldOffset == 40);
    std::memcpy(cmd.bytes + 48, &current, sizeof(current));
    std::memcpy(cmd.bytes + 52, &total, sizeof(total));
}

void writeArena(FakeCmd& cmd, void* arena)
{
    std::memcpy(cmd.bytes + 40, &arena, sizeof(arena));
}

void writeRep(FakeCmd& cmd, FakeCmd& owner, int current, int allocated)
{
    
    
    for (int i = 0; i < allocated; ++i) {
        void* pointer = reinterpret_cast<std::byte*>(owner.entry(static_cast<std::size_t>(i)));
        std::memcpy(cmd.slot(static_cast<std::size_t>(i)), &pointer, sizeof(pointer));
    }
    for (std::size_t i = static_cast<std::size_t>(allocated); i < cmd.entries.size(); ++i) {
        void* pointer = nullptr;
        std::memcpy(cmd.slot(i), &pointer, sizeof(pointer));
    }
    std::memcpy(cmd.rep + cs2::CUserCmd::InputHistory::kRepAllocatedSizeOffset, &allocated, sizeof(allocated));
    std::byte* repPointer = owner.rep;
    std::memcpy(cmd.bytes + cs2::CUserCmd::InputHistory::kRepOffset, &repPointer, sizeof(repPointer));
}

FakeCmd makeValidField(int current, int total, int allocated)
{
    FakeCmd cmd;
    writeArena(cmd, nullptr); 
    writeCounts(cmd, current, total);
    writeRep(cmd, cmd, current, allocated);
    return cmd;
}

struct RecordingAllocator {
    int calls{0};

    void* operator()(void* field, void*) noexcept
    {
        ++calls;
        int current{};
        std::memcpy(&current, static_cast<std::byte*>(field) + 8, sizeof(current));
        current += 1;
        std::memcpy(static_cast<std::byte*>(field) + 8, &current, sizeof(current));
        return field;
    }

    explicit operator bool() const noexcept { return true; }
};

} 

TEST(InputHistoryTest, NullCommandIsInvalidAndCountless)
{
    InputHistory history{nullptr};
    EXPECT_FALSE(history.looksValid());
    EXPECT_EQ(history.currentSize(), -1);
    EXPECT_EQ(history.freeSlots(), 0);
    EXPECT_EQ(history.spareSlots(), 0);
    EXPECT_EQ(history.entryAt(0), nullptr);
}

TEST(InputHistoryTest, GameShapedFieldValidates)
{
    FakeCmd cmd = makeValidField(3, 32, 5);
    InputHistory history{asCmd(cmd)};
    ASSERT_TRUE(history.looksValid());
    EXPECT_EQ(history.currentSize(), 3);
    EXPECT_EQ(history.freeSlots(), cs2::CUserCmd::kMaxInputHistoryEntries - 3);
    EXPECT_EQ(history.spareSlots(), 2); 
    EXPECT_NE(history.entryAt(2), nullptr);
    EXPECT_EQ(history.entryAt(3), nullptr);
}

TEST(InputHistoryTest, ProtobufInvariantsAreEnforced)
{
    
    FakeCmd inverted = makeValidField(5, 4, 5);
    EXPECT_FALSE(InputHistory{asCmd(inverted)}.looksValid());

    
    FakeCmd underOwned = makeValidField(3, 32, 2);
    EXPECT_FALSE(InputHistory{asCmd(underOwned)}.looksValid());

    
    FakeCmd oversize = makeValidField(1, 100000, 1);
    EXPECT_FALSE(InputHistory{asCmd(oversize)}.looksValid());

    
    FakeCmd noRep = makeValidField(1, 32, 1);
    std::byte* nullRep = nullptr;
    std::memcpy(noRep.bytes + 56, &nullRep, sizeof(nullRep));
    EXPECT_FALSE(InputHistory{asCmd(noRep)}.looksValid());

    
    FakeCmd skewed = makeValidField(1, 32, 1);
    std::byte* skewedRep = skewed.rep + 3;
    std::memcpy(skewed.bytes + 56, &skewedRep, sizeof(skewedRep));
    EXPECT_FALSE(InputHistory{asCmd(skewed)}.looksValid());
}

TEST(InputHistoryTest, FastPathPublishesWithConservingSwap)
{
    FakeCmd cmd = makeValidField(1, 32, 3);
    InputHistory history{asCmd(cmd)};
    ASSERT_TRUE(history.looksValid());
    ASSERT_EQ(history.spareSlots(), 2);

    RecordingAllocator allocator;
    Entry freshEntry{};
    const auto result = history.publish(reinterpret_cast<std::byte*>(&freshEntry), allocator);
    ASSERT_EQ(result, InputHistory::PublishResult::PublishedFastPath);

    
    Entry* republished{};
    std::memcpy(&republished, cmd.slot(1), sizeof(republished));
    EXPECT_EQ(republished, &freshEntry);
    EXPECT_EQ(history.currentSize(), 2);

    int newAllocated{};
    std::memcpy(&newAllocated, cmd.rep + cs2::CUserCmd::InputHistory::kRepAllocatedSizeOffset, sizeof(newAllocated));
    EXPECT_EQ(newAllocated, 4);

    
    Entry* preserved{};
    std::memcpy(&preserved, cmd.slot(3), sizeof(preserved));
    EXPECT_EQ(preserved, &cmd.entries[1]);
}

TEST(InputHistoryTest, RecycleBranchOverwritesOnlyOnArenalessFields)
{
    
    
    
    
    FakeCmd cmd = makeValidField(2, 4, 4);
    InputHistory history{asCmd(cmd)};
    ASSERT_TRUE(history.looksValid());

    Entry recycled{};
    RecordingAllocator allocator;
    const auto result = history.publish(reinterpret_cast<std::byte*>(&recycled), allocator);
    ASSERT_EQ(result, InputHistory::PublishResult::PublishedSlotRecycle);

    Entry* visible{};
    std::memcpy(&visible, cmd.slot(2), sizeof(visible));
    EXPECT_EQ(visible, &recycled);
    EXPECT_EQ(history.currentSize(), 3);

    int newAllocated{};
    std::memcpy(&newAllocated, cmd.rep + cs2::CUserCmd::InputHistory::kRepAllocatedSizeOffset, sizeof(newAllocated));
    EXPECT_EQ(newAllocated, 4); 
    EXPECT_EQ(allocator.calls, 0);
}

TEST(InputHistoryTest, ExhaustedArenaBackedRepGoesThroughTheGameHelper)
{
    
    
    FakeCmd cmd = makeValidField(4, 4, 4);
    writeArena(cmd, reinterpret_cast<void*>(static_cast<std::uintptr_t>(0x10000)));
    InputHistory history{asCmd(cmd)};

    Entry freshEntry{};
    RecordingAllocator allocator;
    const auto result = history.publish(reinterpret_cast<std::byte*>(&freshEntry), allocator);
    ASSERT_EQ(result, InputHistory::PublishResult::PublishedByGame);
    EXPECT_EQ(allocator.calls, 1);
    EXPECT_EQ(history.currentSize(), 5);
}

TEST(InputHistoryTest, SilentGameHelperIsRefusedNotTrusted)
{
    FakeCmd cmd = makeValidField(4, 4, 4);
    writeArena(cmd, reinterpret_cast<void*>(static_cast<std::uintptr_t>(0x10000)));
    InputHistory history{asCmd(cmd)};

    struct LyingAllocator {
        void* operator()(void*, void*) const noexcept { return nullptr; }
        explicit operator bool() const noexcept { return true; }
    };

    Entry freshEntry{};
    EXPECT_EQ(history.publish(reinterpret_cast<std::byte*>(&freshEntry), LyingAllocator{}),
              InputHistory::PublishResult::Refused);
    EXPECT_EQ(history.currentSize(), 4);
}

TEST(InputHistoryTest, VirginFieldIsValidAndGrowsThroughTheGameHelper)
{
    
    
    
    alignas(8) std::byte bytes[256]{};
    InputHistory history{reinterpret_cast<cs2::CUserCmd*>(bytes)};
    ASSERT_TRUE(history.looksValid());
    EXPECT_EQ(history.currentSize(), 0);
    EXPECT_EQ(history.spareSlots(), 0);
    EXPECT_EQ(history.freeSlots(), cs2::CUserCmd::kMaxInputHistoryEntries);

    struct GrowingAllocator {
        void* operator()(void* field, void*) const noexcept
        {
            int current{};
            std::memcpy(&current, static_cast<std::byte*>(field) + 8, sizeof(current));
            current += 1;
            std::memcpy(static_cast<std::byte*>(field) + 8, &current, sizeof(current));
            return field;
        }
        explicit operator bool() const noexcept { return true; }
    };

    Entry fresh{};
    EXPECT_EQ(history.publish(reinterpret_cast<std::byte*>(&fresh), GrowingAllocator{}),
              InputHistory::PublishResult::PublishedByGame);
    EXPECT_EQ(history.currentSize(), 1);
}

TEST(InputHistoryTest, FixedBufferCeilingIsHard)
{
    constexpr int kMax = cs2::CUserCmd::kMaxInputHistoryEntries;
    FakeCmd cmd = makeValidField(kMax, kMax, kMax);
    writeArena(cmd, reinterpret_cast<void*>(static_cast<std::uintptr_t>(0x10000)));
    InputHistory history{asCmd(cmd)};

    struct Boom {
        void* operator()(void*, void*) const noexcept { ADD_FAILURE() << "allocator must not be reached"; return nullptr; }
        explicit operator bool() const noexcept { return true; }
    };

    Entry freshEntry{};
    EXPECT_EQ(history.publish(reinterpret_cast<std::byte*>(&freshEntry), Boom{}),
              InputHistory::PublishResult::Refused);
}
