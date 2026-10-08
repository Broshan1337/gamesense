#include <gtest/gtest.h>
#include <Features/Visuals/ViewmodelMod/ViewmodelRotation.h>

TEST(ViewmodelRotationTests, RotatesPrivatePoseAndLeavesCameraAndYawUnchanged)
{
    const cs2::Vector camera{35, 127, 2};
    const auto pose = viewmodel_rotation::apply(camera, -90, 180);
    EXPECT_FLOAT_EQ(pose.x, -55);
    EXPECT_FLOAT_EQ(pose.y, 127);
    EXPECT_FLOAT_EQ(pose.z, -178);
    EXPECT_FLOAT_EQ(camera.x, 35);
    EXPECT_FLOAT_EQ(camera.y, 127);
    EXPECT_FLOAT_EQ(camera.z, 2);
}
TEST(ViewmodelRotationTests, AllowsUpsideDownPitchAndWrapsWithoutClampingToAimLimits)
{
    const auto pose = viewmodel_rotation::apply({80, 20, 0}, 180, -180);
    EXPECT_FLOAT_EQ(pose.x, -100);
    EXPECT_FLOAT_EQ(pose.z, -180);
}
TEST(ViewmodelRotationTests, ZeroOverridesPreserveEnginePoseExactly)
{
    const auto pose = viewmodel_rotation::apply({275, 400, 270}, 0, 0);
    EXPECT_FLOAT_EQ(pose.x, 275);
    EXPECT_FLOAT_EQ(pose.y, 400);
    EXPECT_FLOAT_EQ(pose.z, 270);
}
TEST(ViewmodelRotationTests, RejectsNonfiniteOverrides)
{
    const auto pose = viewmodel_rotation::apply({10, 20, 30}, std::numeric_limits<float>::quiet_NaN(), 180);
    EXPECT_FLOAT_EQ(pose.x, 10);
    EXPECT_FLOAT_EQ(pose.z, 30);
}
