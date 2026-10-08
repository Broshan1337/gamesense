#include <limits>
#include <gtest/gtest.h>
#include <Features/Combat/LegitAimbot/AimAssist.h>
#include <Features/Visuals/AnimationMods/ConVarOverride.h>
TEST(AimAssistTest, SmoothResponseIsIndependentOfTickSubdivision) {
    aim_assist::Parameters p; p.strength = 100; p.speed = 720;
    const auto one = aim_assist::step(0, 10, 1.0f / 64, p);
    const auto half = aim_assist::step(0, 10, 1.0f / 128, p);
    const auto second = aim_assist::step(0, 10 - half.yaw, 1.0f / 128, p);
    EXPECT_NEAR(one.yaw, half.yaw + second.yaw, 0.0001f);
}
TEST(AimAssistTest, SnapStaysWithinItsRadiusAndSmoothNeverOvershoots) {
    aim_assist::Parameters p; p.mode = aim_assist::Mode::Snap; p.snapFov = 2;
    EXPECT_FLOAT_EQ(aim_assist::step(0, 1, 1.0f / 64, p).yaw, 1);
    EXPECT_LT(aim_assist::step(0, 5, 1.0f / 64, p).yaw, 5);
    p.mode = aim_assist::Mode::Smooth;
    const auto step = aim_assist::step(10, 20, 1.0f / 64, p);
    EXPECT_LT(step.pitch, 10); EXPECT_LT(step.yaw, 20);
    EXPECT_LE(std::hypot(step.pitch, step.yaw), p.speed / 64 + 0.00001f);
}
TEST(AimAssistTest, MagnetHasLessPullAtOuterEdgeAndHonorsDeadzone) {
    aim_assist::Parameters p; p.mode = aim_assist::Mode::Magnet; p.speed = 720;
    EXPECT_GT(aim_assist::step(0, 1, 1.0f / 64, p).yaw, aim_assist::step(0, 3, 1.0f / 64, p).yaw / 3);
    EXPECT_FLOAT_EQ(aim_assist::step(0, 0.01f, 1.0f / 64, p).yaw, 0);
    EXPECT_FLOAT_EQ(aim_assist::step(0, std::numeric_limits<float>::quiet_NaN(), 1, p).yaw, 0);
}
TEST(AimAssistTest, AcquisitionWaitsForReactionAndTargetSwitchAndResetsClockRollback) {
    aim_assist::Acquisition state;
    EXPECT_FALSE(state.ready(1, 1, .1f, .2f)); EXPECT_TRUE(state.ready(1, 1.11f, .1f, .2f));
    EXPECT_FALSE(state.ready(2, 2, .1f, .2f)); EXPECT_FALSE(state.ready(2, 2.11f, .1f, .2f));
    EXPECT_TRUE(state.ready(2, 2.21f, .1f, .2f)); EXPECT_FALSE(state.ready(2, 0, .1f, .2f));
}
TEST(AnimationOverrideTest, RestoresOriginalAfterDisableAndDoesNotGuessMissingCvar) {
    convar_override::State<float> state; float value = 3;
    auto read = [&] { return std::optional<float>{value}; };
    auto write = [&](float v) { value = v; return true; };
    state.update(true, 5.f, read, write); EXPECT_FLOAT_EQ(value, 5);
    state.update(true, 7.f, read, write); EXPECT_FLOAT_EQ(value, 7);
    state.update(false, 0.f, read, write); EXPECT_FLOAT_EQ(value, 3);
    state.update(true, 9.f, [] { return std::optional<float>{}; }, write); EXPECT_FLOAT_EQ(value, 3);
}
