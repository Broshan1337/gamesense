#include <gtest/gtest.h>

#include <cstring>

#include <GameClient/UserCmd.h>







namespace {

struct FakeCommand {
    alignas(8) std::byte cmd[192]{};
    alignas(8) std::byte base[160]{};

    explicit FakeCommand()
    {
        std::byte* basePointer = base;
        std::memcpy(cmd + cs2::CUserCmd::kBaseMessageOffset, &basePointer, sizeof(basePointer));
    }

    [[nodiscard]] cs2::CUserCmd* handle() noexcept { return reinterpret_cast<cs2::CUserCmd*>(cmd); }

    [[nodiscard]] std::uint32_t baseHasBits() const noexcept
    {
        std::uint32_t bits{};
        std::memcpy(&bits, base + cs2::CUserCmd::BaseMessage::kHasBitsOffset, sizeof(bits));
        return bits;
    }
};

} 

TEST(UserCmdRandomSeedTest, UnsetSeedReadsBackEmptyNotZero)
{
    FakeCommand command;
    const UserCmd userCmd{command.handle()};
    ASSERT_TRUE(static_cast<bool>(userCmd));

    EXPECT_FALSE(userCmd.randomSeed().hasValue());
}

TEST(UserCmdRandomSeedTest, WriteMarksHasBitAndReadsBackValue)
{
    FakeCommand command;
    const UserCmd userCmd{command.handle()};

    userCmd.setRandomSeed(1234567);
    EXPECT_EQ(command.baseHasBits() & cs2::CUserCmd::BaseMessage::kRandomSeedHasBit,
              cs2::CUserCmd::BaseMessage::kRandomSeedHasBit);

    const auto seed{userCmd.randomSeed()};
    ASSERT_TRUE(seed.hasValue());
    EXPECT_EQ(seed.value(), 1234567);
}

TEST(UserCmdRandomSeedTest, ZeroIsALegitimateSeedOnceMarked)
{
    FakeCommand command;
    const UserCmd userCmd{command.handle()};

    userCmd.setRandomSeed(0);
    ASSERT_TRUE(userCmd.randomSeed().hasValue());
    EXPECT_EQ(userCmd.randomSeed().value(), 0);
}

TEST(UserCmdRandomSeedTest, NegativeSeedsRoundTripBitExact)
{
    FakeCommand command;
    const UserCmd userCmd{command.handle()};

    userCmd.setRandomSeed(-1);
    ASSERT_TRUE(userCmd.randomSeed().hasValue());
    EXPECT_EQ(userCmd.randomSeed().value(), -1);
}

TEST(UserCmdRandomSeedTest, MissingBaseMessageIsANoOp)
{
    alignas(8) std::byte empty[128]{}; 
    const UserCmd userCmd{reinterpret_cast<cs2::CUserCmd*>(empty)};
    EXPECT_FALSE(static_cast<bool>(userCmd));

    
    userCmd.setRandomSeed(7);
    EXPECT_FALSE(userCmd.randomSeed().hasValue());
}
