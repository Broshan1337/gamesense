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

TEST(RecoilCompensationTest, RejectsNonFinitePunchAndClampsEntryPitch)
{
    CommandFixture command;
    command.history();
    recoil_compensation::apply(command.handle(), std::numeric_limits<float>::quiet_NaN(), 0);
    EXPECT_FLOAT_EQ(CommandFixture::read<float>(command.samples[0], 24), 20.0f);
    EXPECT_FLOAT_EQ(UserCmd{command.handle()}.viewPitch().value(), 20.0f);
    recoil_compensation::apply(command.handle(), 200, 0);
    EXPECT_FLOAT_EQ(CommandFixture::read<float>(command.samples[0], 24), -89.0f);
}

TEST(AttackCommandTest, EveryRequestedCommandWritesAllFiringBanks)
{
    int context{};
    AttackCommand<int> attack{context};
    for (int i = 0; i < 3; ++i) {
        CommandFixture command;
        command.history();
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
    command.history();
    CommandFixture::pointer(command.base, cs2::CUserCmd::BaseMessage::kButtonsPbOffset, nullptr);
    EXPECT_TRUE(AttackCommand<int>{context}.press(command.handle()));
    EXPECT_EQ(CommandFixture::read<std::uint64_t>(command.cmd, 96) & 1, 1u);
    EXPECT_EQ(CommandFixture::read<std::uint64_t>(command.cmd, 104) & 1, 1u);
}

TEST(AttackCommandTest, AttackWithoutHistoryIsRefusedNotSentMalformed)
{
    // 2026-10-15 contract (the "no attack information present in command /
    // attack1_start_history_index: -1" server warnings): an attack press
    // without a readable input history must be REFUSED - sending it would
    // degrade server lag comp to the fallback path. Callers run per frame
    // and fire a frame later with a complete attack record.
    int context{};
    CommandFixture command;
    EXPECT_FALSE(AttackCommand<int>{context}.press(command.handle()));
}

TEST(AttackCommandTest, PressStampsAttackStartHistoryIndexSizeMinusOne)
{
    int context{};
    CommandFixture command;
    command.history();
    ASSERT_TRUE(AttackCommand<int>{context}.press(command.handle()));
    EXPECT_EQ(CommandFixture::read<int>(command.cmd, cs2::CUserCmd::kAttack1StartHistoryIndexOffset), 1);
}

TEST(RecoilApplyTest, FullPunchSubtractedFromEveryEntry)
{
    // The 2026-10-13 rewrite: the compensation rides the input-history ENTRY
    // angles (the shot reconstruction surface), subtracting the FULL current
    // punch - not the cmd BASE angles (the camera) and not a per-command delta.
    CommandFixture command;
    command.history();
    recoil_compensation::apply(command.handle(), -2.0f, 1.0f);
    EXPECT_FLOAT_EQ(CommandFixture::read<float>(command.samples[0], 24), 22.0f);
    EXPECT_FLOAT_EQ(CommandFixture::read<float>(command.samples[0], 28), 178.0f);
    EXPECT_FLOAT_EQ(CommandFixture::read<float>(command.samples[1], 24), 23.0f);
    EXPECT_FLOAT_EQ(CommandFixture::read<float>(command.samples[1], 28), 179.0f);
}

TEST(RecoilApplyTest, CameraBaseAnglesUntouched)
{
    // The friend report "it moves the screen down": the camera IS the cmd base
    // angles - compensation must never touch them.
    CommandFixture command;
    command.history();
    recoil_compensation::apply(command.handle(), -2.0f, 1.0f);
    EXPECT_FLOAT_EQ(UserCmd{command.handle()}.viewPitch().value(), 20.0f);
    EXPECT_FLOAT_EQ(UserCmd{command.handle()}.viewYaw().value(), 179.0f);
}

TEST(RecoilApplyTest, PerFrameRebuildKeepsConstantKickFromAccumulating)
{
    // The game rebuilds the command (base + entries) from the player's real
    // input every frame, and CreateMove runs every frame on the in-progress
    // command - so apply() always sees FRESH entry angles. Model the rebuild
    // between applies: a constant punch must not accumulate across frames
    // (the old delta math was the accumulating kind).
    CommandFixture command;
    for (int frame = 0; frame < 5; ++frame) {
        command.history();
        recoil_compensation::apply(command.handle(), -2.0f, 1.0f);
    }
    EXPECT_FLOAT_EQ(CommandFixture::read<float>(command.samples[0], 24), 22.0f);
    EXPECT_FLOAT_EQ(CommandFixture::read<float>(command.samples[0], 28), 178.0f);
    EXPECT_FLOAT_EQ(CommandFixture::read<float>(command.samples[1], 24), 23.0f);
}
