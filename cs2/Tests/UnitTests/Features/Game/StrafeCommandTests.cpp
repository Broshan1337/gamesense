#include <gtest/gtest.h>

#include <array>
#include <cstring>
#include <limits>

#include <Features/Game/StrafeCommand.h>

namespace {
struct Context {};
using Buttons = cs2::CCSGOInput::Buttons;
using Step = cs2::CSubtickMoveStep;
using Field = cs2::CUserCmd::BaseMessage::SubtickMoves;
using Pb = cs2::CUserCmd::BaseMessage::ButtonsPb;
constexpr auto moveMask = Buttons::kForward | Buttons::kBack | Buttons::kMoveLeft | Buttons::kMoveRight;

template <typename T, std::size_t N>
void write(std::array<std::byte, N>& object, int offset, T value)
{
    std::memcpy(object.data() + offset, &value, sizeof(value));
}
template <typename T, std::size_t N>
T read(const std::array<std::byte, N>& object, int offset)
{
    T value{};
    std::memcpy(&value, object.data() + offset, sizeof(value));
    return value;
}
struct Command {
    std::array<std::byte, 192> raw{};
    std::array<std::byte, 192> base{};
    std::array<std::byte, 64> angles{};
    std::array<std::byte, 64> buttons{};
    std::array<std::byte, 32> rep{};
    std::array<std::array<std::byte, 64>, 3> steps{};
    Command()
    {
        write(raw, cs2::CUserCmd::kBaseMessageOffset, base.data());
        write(raw, cs2::CUserCmd::kCommandNumberOffset, 42);
        write(base, cs2::CUserCmd::BaseMessage::kViewAnglesOffset, angles.data());
        write(base, cs2::CUserCmd::BaseMessage::kButtonsPbOffset, buttons.data());
    }
    cs2::CUserCmd* handle() { return reinterpret_cast<cs2::CUserCmd*>(raw.data()); }
    void subticks()
    {
        write(base, Field::kFieldOffset + Field::kCurrentSizeOffset, 3);
        write(base, Field::kFieldOffset + Field::kRepOffset, rep.data());
        write(rep, Field::kRepAllocatedSizeOffset, 3);
        for (int i = 0; i < 3; ++i) {
            write(rep, Field::kRepElementsOffset + i * int(sizeof(std::byte*)), steps[i].data());
            write(steps[i], Step::kHasBitsOffset, Step::kButtonHasBit | Step::kPressedHasBit
                | Step::kAnalogForwardDeltaHasBit | Step::kAnalogLeftDeltaHasBit | Step::kWhenHasBit | Step::kYawDeltaHasBit);
            write(steps[i], Step::kWhenOffset, 0.25f * (i + 1));
            write(steps[i], Step::kYawDeltaOffset, 5.0f);
            write(steps[i], Step::kPressedOffset, std::uint8_t{1});
            write(steps[i], Step::kAnalogForwardDeltaOffset, -1.0f);
            write(steps[i], Step::kAnalogLeftDeltaOffset, 1.0f);
        }
        write(steps[0], Step::kButtonOffset, Buttons::kForward);
        write(steps[1], Step::kButtonOffset, Buttons::kJump);
        write(steps[2], Step::kButtonOffset, Buttons::kMoveLeft | Buttons::kAttack);
    }
};
}

TEST(StrafeCommandTest, ReplacesAnalogAndAllMovementBanksButPreservesOtherButtons)
{
    Command cmd;
    for (int offset : {cs2::CUserCmd::kButtonState1Offset, cs2::CUserCmd::kButtonState2Offset,
            cs2::CUserCmd::kButtonState3Offset})
        write(cmd.raw, offset, moveMask | Buttons::kJump | Buttons::kAttack);
    for (int offset : {Pb::kButtonState1Offset, Pb::kButtonState2Offset, Pb::kButtonState3Offset})
        write(cmd.buttons, offset, moveMask | Buttons::kJump | Buttons::kAttack);
    StrafeCommand pending;
    ASSERT_TRUE(pending.stage(cmd.handle(), {0.0f, -1.0f}));
    ASSERT_TRUE(pending.commit<Context>(cmd.handle()));
    EXPECT_FLOAT_EQ(UserCmd{cmd.handle()}.forwardMove().value(), 0.0f);
    EXPECT_FLOAT_EQ(UserCmd{cmd.handle()}.leftMove().value(), -1.0f);
    const auto expected = Buttons::kMoveRight | Buttons::kJump | Buttons::kAttack;
    for (int offset : {cs2::CUserCmd::kButtonState1Offset, cs2::CUserCmd::kButtonState2Offset})
        EXPECT_EQ(read<std::uint64_t>(cmd.raw, offset), expected);
    for (int offset : {Pb::kButtonState1Offset, Pb::kButtonState2Offset})
        EXPECT_EQ(read<std::uint64_t>(cmd.buttons, offset), expected);
    EXPECT_EQ(read<std::uint64_t>(cmd.raw, cs2::CUserCmd::kButtonState3Offset), Buttons::kJump | Buttons::kAttack);
    EXPECT_EQ(read<std::uint64_t>(cmd.buttons, Pb::kButtonState3Offset), Buttons::kJump | Buttons::kAttack);
    const auto hasBits = read<std::uint32_t>(cmd.base, cs2::CUserCmd::BaseMessage::kHasBitsOffset);
    EXPECT_EQ(hasBits & (cs2::CUserCmd::BaseMessage::kForwardMoveHasBit | cs2::CUserCmd::BaseMessage::kLeftMoveHasBit),
        cs2::CUserCmd::BaseMessage::kForwardMoveHasBit | cs2::CUserCmd::BaseMessage::kLeftMoveHasBit);
}

TEST(StrafeCommandTest, CommitLeavesRealSubtickSamplesIntactSoReplayedKeysNeverStick)
{
    Command cmd;
    cmd.subticks();
    const auto snapshot = cmd.steps;
    StrafeCommand pending;
    ASSERT_TRUE(pending.stage(cmd.handle(), {0.0f, -1.0f}));
    ASSERT_TRUE(pending.commit<Context>(cmd.handle()));
    // The replay consumer applies subtick samples with carried state: erasing a
    // real press+release pair (the old stripMovement) stranded the key as held
    // until the player pressed it again.
    EXPECT_EQ(read<int>(cmd.base, Field::kFieldOffset + Field::kCurrentSizeOffset), 3);
    EXPECT_EQ(cmd.steps, snapshot);
    EXPECT_FLOAT_EQ(UserCmd{cmd.handle()}.forwardMove().value(), 0.0f);
    EXPECT_FLOAT_EQ(UserCmd{cmd.handle()}.leftMove().value(), -1.0f);
}

TEST(StrafeCommandTest, CorrectsMovementWhenAnotherFeatureChangesViewYaw)
{
    Command cmd;
    StrafeCommand pending;
    ASSERT_TRUE(pending.stage(cmd.handle(), {1.0f, 0.0f}));
    write(cmd.angles, cs2::CUserCmd::BaseMessage::ViewAngles::kYawOffset, 90.0f);
    ASSERT_TRUE(pending.commit<Context>(cmd.handle()));
    EXPECT_FLOAT_EQ(UserCmd{cmd.handle()}.forwardMove().value(), 0.0f);
    EXPECT_NEAR(UserCmd{cmd.handle()}.leftMove().value(), -1.0f, 0.00001f);
}

TEST(StrafeCommandTest, ConsumesPendingInputOnlyOnce)
{
    Command cmd;
    StrafeCommand pending;
    ASSERT_TRUE(pending.stage(cmd.handle(), {1.0f, 1.0f}));
    ASSERT_TRUE(pending.commit<Context>(cmd.handle()));
    EXPECT_NEAR(UserCmd{cmd.handle()}.forwardMove().value(), std::sqrt(0.5f), 0.00001f);
    const auto saved = cmd.base;
    EXPECT_FALSE(pending.commit<Context>(cmd.handle()));
    EXPECT_EQ(cmd.base, saved);
}

TEST(StrafeCommandTest, RejectsDifferentCommandsReusedSequencesAndReplacedBaseMessages)
{
    for (int change = 0; change < 3; ++change) {
        Command cmd, other;
        StrafeCommand pending;
        ASSERT_TRUE(pending.stage(cmd.handle(), {1.0f, 0.0f}));
        auto* target = cmd.handle();
        if (change == 0) target = other.handle();
        if (change == 1) write(cmd.raw, cs2::CUserCmd::kCommandNumberOffset, 43);
        if (change == 2) write(cmd.raw, cs2::CUserCmd::kBaseMessageOffset, other.base.data());
        const auto saved = cmd.base;
        const auto savedOther = other.base;
        EXPECT_FALSE(pending.commit<Context>(target));
        EXPECT_FALSE(pending.active());
        EXPECT_EQ(cmd.base, saved);
        EXPECT_EQ(other.base, savedOther);
    }
}

TEST(StrafeCommandTest, RejectsInvalidYawMovementAndMissingCommands)
{
    Command cmd;
    StrafeCommand pending;
    EXPECT_FALSE(pending.stage(nullptr, {1.0f, 0.0f}));
    EXPECT_FALSE(pending.stage(cmd.handle(), {}));
    EXPECT_FALSE(pending.stage(cmd.handle(), {std::numeric_limits<float>::infinity(), 0.0f}));
    write(cmd.angles, cs2::CUserCmd::BaseMessage::ViewAngles::kYawOffset, std::numeric_limits<float>::quiet_NaN());
    EXPECT_FALSE(pending.stage(cmd.handle(), {1.0f, 0.0f}));
    write(cmd.angles, cs2::CUserCmd::BaseMessage::ViewAngles::kYawOffset, 0.0f);
    ASSERT_TRUE(pending.stage(cmd.handle(), {1.0f, 0.0f}));
    write(cmd.angles, cs2::CUserCmd::BaseMessage::ViewAngles::kYawOffset, std::numeric_limits<float>::quiet_NaN());
    const auto saved = cmd.base;
    EXPECT_FALSE(pending.commit<Context>(cmd.handle()));
    EXPECT_EQ(cmd.base, saved);
}

TEST(StrafeCommandTest, WeakAssistPreservesMagnitudeRatherThanForcingFullInput) {
    Command cmd; StrafeCommand pending;
    ASSERT_TRUE(pending.stage(cmd.handle(), {0, .25f}));
    ASSERT_TRUE(pending.commit<Context>(cmd.handle()));
    EXPECT_FLOAT_EQ(UserCmd{cmd.handle()}.leftMove().value(), .25f);
}
TEST(StrafeCommandTest, JumpEditingPreservesAnalogLookAndOtherButtonsAndSortsEvents) {
    Command cmd; cmd.subticks();
    SubtickMoves<Context>::stripButtons(cmd.base.data(), Buttons::kJump);
    EXPECT_EQ(read<std::uint32_t>(cmd.steps[1], Step::kHasBitsOffset) & Step::kButtonHasBit, 0);
    EXPECT_NE(read<std::uint32_t>(cmd.steps[1], Step::kHasBitsOffset) & Step::kAnalogForwardDeltaHasBit, 0);
    EXPECT_FLOAT_EQ(read<float>(cmd.steps[1], Step::kYawDeltaOffset), 5);
    EXPECT_EQ(read<std::uint64_t>(cmd.steps[0], Step::kButtonOffset), Buttons::kForward);
    write(cmd.steps[0], Step::kWhenOffset, .9f);
    SubtickMoves<Context>::sortByTime(cmd.base.data());
    EXPECT_EQ(read<std::byte*>(cmd.rep, Field::kRepElementsOffset), cmd.steps[1].data());
    EXPECT_EQ(read<std::byte*>(cmd.rep, Field::kRepElementsOffset + 2 * sizeof(std::byte*)), cmd.steps[0].data());
}
