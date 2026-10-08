#include <gtest/gtest.h>
#include <Features/Game/JumpTiming.h>
TEST(JumpTimingTest, AvoidsSpamCooldownAndHandlesClockRollback) {
    jump_timing::State state;
    EXPECT_TRUE(state.mayPress(10, 1.0f / 64)); state.pressed(10);
    EXPECT_FALSE(state.mayPress(10, 1.0f / 64));
    EXPECT_FALSE(state.mayPress(10 + 1.0 / 64, 1.0f / 64));
    EXPECT_TRUE(state.mayPress(10 + 1.0 / 64 + 1.0 / 4096 + .000001, 1.0f / 64));
    EXPECT_TRUE(state.mayPress(1, 1.0f / 64));
}
TEST(JumpTimingTest, LandingTimesAreQuantizedInsideTick) {
    EXPECT_FLOAT_EQ(jump_timing::landingWhen(.3f), 19.f / 64);
    EXPECT_FLOAT_EQ(jump_timing::landingWhen(0), 1.f / 64);
    EXPECT_FLOAT_EQ(jump_timing::landingWhen(1), 63.f / 64);
}
