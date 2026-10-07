#include <gtest/gtest.h>

#include <cmath>
#include <limits>

#include <Features/Game/AirStrafe.h>

namespace {

// Independent scalar reference for Valve's capped air acceleration.
float speedSquaredAfter(float speed, float angle, const air_strafe::Parameters& p)
{
    const float projection = speed * std::cos(angle);
    const float added = std::min(p.wishSpeed, p.airWishSpeedCap) - projection;
    if (added <= 0.0f)
        return speed * speed;
    const float acceleration = std::min(added, p.airAccelerate * p.wishSpeed * p.frameTime * p.surfaceFriction);
    return speed * speed + 2.0f * projection * acceleration + acceleration * acceleration;
}

constexpr air_strafe::Parameters defaults{250.0f, 12.0f, 30.0f, 1.0f / 64.0f, 1.0f};

}

TEST(AirStrafeTest, MatchesMaximumSpeedGainAcrossMovementConditions)
{
    for (float speed : {1.0f, 5.0f, 30.0f, 250.0f, 1000.0f}) {
        for (float dt : {1.0f / 64.0f, 1.0f / 128.0f, 1.0f / 1024.0f}) {
            for (float friction : {0.25f, 1.0f}) {
                for (float wishSpeed : {10.0f, 100.0f, 250.0f}) {
                    const air_strafe::Parameters p{wishSpeed, 12.0f, 30.0f, dt, friction};
                    const float gain = speedSquaredAfter(speed, air_strafe::idealAngle(speed, p), p);
                    float best = speed * speed;
                    for (int i = 0; i <= 1800; ++i)
                        best = std::max(best, speedSquaredAfter(speed, float(i) * trig::kPi / 1800.0f, p));
                    EXPECT_GE(gain + 0.2f, best) << "speed=" << speed << " dt=" << dt << " friction=" << friction;
                }
            }
        }
    }
}

TEST(AirStrafeTest, AccountsForFrictionAndTickDuration)
{
    auto p = defaults;
    EXPECT_NEAR(air_strafe::idealAngle(250.0f, p), trig::kHalfPi, 0.0001f);
    p.surfaceFriction = 0.25f;
    const float reduced = air_strafe::idealAngle(250.0f, p);
    EXPECT_LT(reduced, trig::kHalfPi);
    p.frameTime *= 0.5f;
    EXPECT_LT(air_strafe::idealAngle(250.0f, p), reduced);
}

TEST(AirStrafeTest, RejectsInvalidPhysics)
{
    for (float invalid : {0.0f, -1.0f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()}) {
        for (int field = 0; field < 5; ++field) {
            auto p = defaults;
            switch (field) {
            case 0: p.wishSpeed = invalid; break;
            case 1: p.airAccelerate = invalid; break;
            case 2: p.airWishSpeedCap = invalid; break;
            case 3: p.frameTime = invalid; break;
            case 4: p.surfaceFriction = invalid; break;
            }
            EXPECT_FALSE(p.valid());
            EXPECT_FLOAT_EQ(air_strafe::idealAngle(250.0f, p), 0.0f);
        }
    }
}

TEST(AirStrafeTest, SteersTowardLeftRightAndBackwardKeys)
{
    bool side = false;
    auto move = air_strafe::steer(250.0f, 0.0f, 0.0f, {0.0f, 1.0f}, 0, side, defaults);
    EXPECT_GT(move.left, 0.99f);
    move = air_strafe::steer(250.0f, 0.0f, 0.0f, {0.0f, -1.0f}, 0, side, defaults);
    EXPECT_LT(move.left, -0.99f);
    move = air_strafe::steer(0.0f, 250.0f, 0.0f, {-1.0f, 0.0f}, 0, side, defaults);
    EXPECT_LT(move.forward, -0.99f);
}

TEST(AirStrafeTest, ExplicitDirectionWinsOverMouseNoiseAndStraightInputAlternates)
{
    bool side = false;
    auto move = air_strafe::steer(250.0f, 0.0f, 0.0f, {0.0f, -1.0f}, -5, side, defaults);
    EXPECT_LT(move.left, -0.99f);
    move = air_strafe::steer(250.0f, 0.0f, 0.0f, {1.0f, 0.0f}, 0, side, defaults);
    EXPECT_GT(move.left, 0.99f);
    move = air_strafe::steer(250.0f, 0.0f, 0.0f, {1.0f, 0.0f}, 0, side, defaults);
    EXPECT_LT(move.left, -0.99f);
}

TEST(AirStrafeTest, HandlesStandingStartsAndDiagonalInput)
{
    bool side = false;
    const auto move = air_strafe::steer(0.0f, 0.0f, 0.0f, {1.0f, 1.0f}, 0, side, defaults);
    EXPECT_NEAR(move.forward, std::sqrt(0.5f), 0.0001f);
    EXPECT_NEAR(move.left, std::sqrt(0.5f), 0.0001f);
    const auto noKeys = air_strafe::steer(0.0f, 0.0f, 0.0f, {}, 0, side, defaults);
    EXPECT_FLOAT_EQ(noKeys.forward, 1.0f);
    EXPECT_FLOAT_EQ(noKeys.left, 0.0f);
}

TEST(AirStrafeTest, MovementIsViewRelativeAndWrapsAcrossYawBoundary)
{
    const auto move = air_strafe::moveAtAngle(179.0f * trig::kDegreesToRadians,
        -179.0f * trig::kDegreesToRadians, 0.0f, true);
    EXPECT_NEAR(move.forward, std::cos(-2.0f * trig::kDegreesToRadians), 0.0001f);
    EXPECT_NEAR(move.left, std::sin(-2.0f * trig::kDegreesToRadians), 0.0001f);
}

TEST(AirStrafeTest, CardinalDirectionsHaveNoResidualOpposingInput)
{
    const auto left = air_strafe::moveAtAngle(0.0f, 0.0f, trig::kHalfPi, true);
    EXPECT_FLOAT_EQ(left.forward, 0.0f);
    EXPECT_NEAR(left.left, 1.0f, 0.00001f);
    const auto back = air_strafe::moveAtAngle(trig::kPi, 0.0f, 0.0f, true);
    EXPECT_FLOAT_EQ(back.left, 0.0f);
    EXPECT_NEAR(back.forward, -1.0f, 0.00001f);
}

TEST(AirStrafeTest, LowSpeedLaunchesTowardInputRatherThanSideways)
{
    bool side = false;
    for (float speed : {0.0f, 1.0f, 5.0f, 29.0f}) {
        const auto move = air_strafe::steer(speed, 0.0f, 0.0f, {1.0f, 0.0f}, 0, side, defaults);
        EXPECT_FLOAT_EQ(move.forward, 1.0f);
        EXPECT_FLOAT_EQ(move.left, 0.0f);
    }
}

TEST(AirStrafeTest, TurningMouseBreaksTiesAlongVelocity)
{
    bool side = false;
    const auto left = air_strafe::steer(250.0f, 0.0f, 0.0f, {}, -3, side, defaults);
    EXPECT_GT(left.left, 0.99f);
    const auto right = air_strafe::steer(250.0f, 0.0f, 0.0f, {}, 3, side, defaults);
    EXPECT_LT(right.left, -0.99f);
}

TEST(AirStrafeTest, SteeringRejectsNonFiniteInputsWithoutChangingSide)
{
    for (float invalid : {std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()}) {
        for (int field = 0; field < 5; ++field) {
            float x = 250.0f, y = 0.0f, yaw = 0.0f;
            air_strafe::Move desired{1.0f, 0.0f};
            switch (field) {
            case 0: x = invalid; break;
            case 1: y = invalid; break;
            case 2: yaw = invalid; break;
            case 3: desired.forward = invalid; break;
            case 4: desired.left = invalid; break;
            }
            bool side = false;
            const auto move = air_strafe::steer(x, y, yaw, desired, 0, side, defaults);
            EXPECT_FLOAT_EQ(move.forward, 0.0f);
            EXPECT_FLOAT_EQ(move.left, 0.0f);
            EXPECT_FALSE(side);
        }
    }
}

TEST(AirStrafeTest, SuccessiveTicksGainSpeedAndTrackRequestedHeading)
{
    float vx = 250.0f, vy = 0.0f;
    bool side = false;
    for (int tick = 0; tick < 128; ++tick) {
        const float target = 30.0f * trig::kDegreesToRadians;
        const auto move = air_strafe::steer(vx, vy, target, {1.0f, 0.0f}, 0, side, defaults);
        const float wx = std::cos(target) * move.forward - std::sin(target) * move.left;
        const float wy = std::sin(target) * move.forward + std::cos(target) * move.left;
        const float oldSpeed = std::hypot(vx, vy);
        const float acceleration = std::min(defaults.airAccelerate * defaults.wishSpeed * defaults.frameTime,
            std::max(0.0f, defaults.airWishSpeedCap - vx * wx - vy * wy));
        vx += acceleration * wx;
        vy += acceleration * wy;
        EXPECT_GE(std::hypot(vx, vy) + 0.001f, oldSpeed);
        EXPECT_NEAR(std::hypot(move.forward, move.left), 1.0f, 0.00001f);
    }
    EXPECT_GT(std::hypot(vx, vy), 400.0f);
    EXPECT_NEAR(std::atan2(vy, vx), 30.0f * trig::kDegreesToRadians, 5.0f * trig::kDegreesToRadians);
}
