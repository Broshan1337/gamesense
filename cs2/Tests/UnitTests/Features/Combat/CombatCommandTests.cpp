#include <gtest/gtest.h>

#include <array>
#include <cstring>
#include <limits>

#include <Features/Combat/AttackCommand.h>
#include <Features/Combat/Rcs/RecoilCompensation.h>

namespace {
struct CommandFixture {
    alignas(8) std::byte cmd[192]{}, base[160]{}, buttons[64]{}, angles[40]{};
    alignas(8) std::byte rep[24]{}, entries[2][128]{}, samples[2][40]{};
    CommandFixture()
    {
        pointer(cmd, cs2::CUserCmd::kBaseMessageOffset, base);
        pointer(base, cs2::CUserCmd::BaseMessage::kViewAnglesOffset, angles);
        pointer(base, cs2::CUserCmd::BaseMessage::kButtonsPbOffset, buttons);
        write(cmd, cs2::CUserCmd::kAttack1StartHistoryIndexOffset, -1);
        UserCmd{handle()}.setViewAngles(20.0f, 179.0f);
    }
    static void pointer(std::byte* storage, int offset, std::byte* value) { write(storage, offset, value); }
    template <typename T> static void write(std::byte* storage, int offset, T value) { std::memcpy(storage + offset, &value, sizeof(value)); }
    template <typename T> static T read(const std::byte* storage, int offset) { T value{}; std::memcpy(&value, storage + offset, sizeof(value)); return value; }
    cs2::CUserCmd* handle() { return reinterpret_cast<cs2::CUserCmd*>(cmd); }
    void history()
    {
        write(cmd, cs2::CUserCmd::InputHistory::kCurrentSizeOffset, 2);
        write(cmd, cs2::CUserCmd::InputHistory::kTotalSizeOffset, 2);
        pointer(cmd, cs2::CUserCmd::InputHistory::kRepOffset, rep);
        write(rep, 0, 2);
        for (int i = 0; i < 2; ++i) {
            pointer(rep, 8 + i * 8, entries[i]);
            pointer(entries[i], cs2::CUserCmd::InputHistory::kEntryViewAnglesOffset, samples[i]);
            write(samples[i], 24, 20.0f + i);
            write(samples[i], 28, 179.0f + i);
        }
    }
};
}

TEST(RecoilCompensationTest, CompensatesEveryFreshCommandEvenWhenPunchIsConstant)
{
    for (int i = 0; i < 3; ++i) {
        CommandFixture command;
        recoil_compensation::apply(command.handle(), 4.0f, -3.0f);
        EXPECT_FLOAT_EQ(UserCmd{command.handle()}.viewPitch().value(), 16.0f);
        EXPECT_FLOAT_EQ(UserCmd{command.handle()}.viewYaw().value(), -178.0f);
    }
}

TEST(RecoilCompensationTest, CorrectsShotHistoryAndPreservesMouseMotion)
{
    CommandFixture command;
    command.history();
    recoil_compensation::apply(command.handle(), 4.0f, 2.0f);
    EXPECT_FLOAT_EQ(CommandFixture::read<float>(command.samples[0], 24), 16.0f);
    EXPECT_FLOAT_EQ(CommandFixture::read<float>(command.samples[1], 24), 17.0f);
    EXPECT_FLOAT_EQ(CommandFixture::read<float>(command.samples[0], 28), 177.0f);
    EXPECT_FLOAT_EQ(CommandFixture::read<float>(command.samples[1], 28), 178.0f);
    EXPECT_EQ(CommandFixture::read<std::uint32_t>(command.entries[1], 16) & 1, 1u);
}

TEST(RecoilCompensationTest, RejectsNonFinitePunchAndClampsPitch)
{
    CommandFixture command;
    recoil_compensation::apply(command.handle(), std::numeric_limits<float>::quiet_NaN(), 0);
    EXPECT_FLOAT_EQ(UserCmd{command.handle()}.viewPitch().value(), 20.0f);
    recoil_compensation::apply(command.handle(), 200, 0);
    EXPECT_FLOAT_EQ(UserCmd{command.handle()}.viewPitch().value(), -89.0f);
}

TEST(RecoilCompensationTest, MalformedHistoryDoesNotPreventBaseCorrection)
{
    CommandFixture command;
    CommandFixture::write(command.cmd, cs2::CUserCmd::InputHistory::kCurrentSizeOffset, 1000);
    recoil_compensation::apply(command.handle(), 4, 0);
    EXPECT_FLOAT_EQ(UserCmd{command.handle()}.viewPitch().value(), 16.0f);
}

TEST(AttackCommandTest, EveryRequestedCommandWritesAllFiringBanks)
{
    int context{};
    AttackCommand<int> attack{context};
    for (int i = 0; i < 3; ++i) {
        CommandFixture command;
        ASSERT_TRUE(attack.press(command.handle()));
        EXPECT_EQ(CommandFixture::read<std::uint64_t>(command.cmd, 96) & 1, 1u);
        EXPECT_EQ(CommandFixture::read<std::uint64_t>(command.cmd, 104) & 1, 1u);
        EXPECT_EQ(CommandFixture::read<std::uint64_t>(command.buttons, 24) & 1, 1u);
        EXPECT_EQ(CommandFixture::read<std::uint64_t>(command.buttons, 32) & 1, 1u);
        EXPECT_TRUE(attack.press(command.handle()));
    }
}

TEST(AttackCommandTest, WritesNativeBanksBeforeButtonProtobufIsAllocated)
{
    int context{};
    CommandFixture command;
    CommandFixture::pointer(command.base, cs2::CUserCmd::BaseMessage::kButtonsPbOffset, nullptr);
    EXPECT_TRUE(AttackCommand<int>{context}.press(command.handle()));
    EXPECT_EQ(CommandFixture::read<std::uint64_t>(command.cmd, 96) & 1, 1u);
    EXPECT_EQ(CommandFixture::read<std::uint64_t>(command.cmd, 104) & 1, 1u);
}
