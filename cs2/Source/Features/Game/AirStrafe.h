#pragma once

#include <algorithm>
#include <cmath>

#include <Utils/Trig.h>

namespace air_strafe
{

struct Parameters {
    float wishSpeed;
    float airAccelerate;
    float airWishSpeedCap;
    float frameTime;
    float surfaceFriction;

    [[nodiscard]] bool valid() const noexcept
    {
        return std::isfinite(wishSpeed) && wishSpeed > 0.0f
            && std::isfinite(airAccelerate) && airAccelerate > 0.0f
            && std::isfinite(airWishSpeedCap) && airWishSpeedCap > 0.0f
            && std::isfinite(frameTime) && frameTime > 0.0f
            && std::isfinite(surfaceFriction) && surfaceFriction > 0.0f;
    }
};

struct Move {
    float forward;
    float left;
};

// Derived from Valve's CGameMovement::AirAccelerate:
// https://github.com/ValveSoftware/source-sdk-2013/blob/master/src/game/shared/gamemovement.cpp
// With p = velocity dot wishdir and a = accel * wishspeed * dt * friction,
// the applied acceleration is min(a, cap - p). Maximum squared-speed gain
// occurs at p = max(0, cap - a), clamped to the current speed.
// Angles in this helper are radians. CS2's positive left input is +world yaw.
[[nodiscard]] inline float idealAngle(float speed, const Parameters& parameters) noexcept
{
    if (!parameters.valid() || !std::isfinite(speed) || speed <= 0.0f)
        return 0.0f;

    const float cap = std::min(parameters.wishSpeed, parameters.airWishSpeedCap);
    const double acceleration = double(parameters.wishSpeed) * parameters.airAccelerate
        * parameters.frameTime * parameters.surfaceFriction;
    const double projection = std::max(0.0, double(cap) - acceleration);
    return static_cast<float>(std::acos(std::clamp(projection / speed, 0.0, 1.0)));
}

[[nodiscard]] inline Move moveAtAngle(float velocityYaw, float viewYaw, float angle, bool left) noexcept
{
    if (!std::isfinite(velocityYaw) || !std::isfinite(viewYaw) || !std::isfinite(angle))
        return {};
    const double direction = std::remainder(double(velocityYaw) - viewYaw + (left ? angle : -angle),
        double(trig::kTwoPi));
    const float forward = static_cast<float>(std::cos(direction));
    const float side = static_cast<float>(std::sin(direction));
    // Polynomial trig leaves small residuals at cardinal angles. Do not turn
    // those residuals into an unintended forward/back or left/right button.
    return {trig::absolute(forward) < 0.0001f ? 0.0f : forward,
        trig::absolute(side) < 0.0001f ? 0.0f : side};
}

// Prefer the optimal branch that turns toward the requested movement direction.
// Explicit movement intent wins over mouse noise. Mouse turning breaks ties when
// looking along the velocity; alternating avoids a permanent left/right bias.
[[nodiscard]] inline Move steer(float velocityX, float velocityY, float viewYaw,
    Move desired, int mouseDx, bool& alternateLeft, const Parameters& parameters) noexcept
{
    if (!parameters.valid() || !std::isfinite(velocityX) || !std::isfinite(velocityY)
        || !std::isfinite(viewYaw) || !std::isfinite(desired.forward) || !std::isfinite(desired.left))
        return {};
    const float speed = std::hypot(velocityX, velocityY);
    if (!std::isfinite(speed))
        return {};
    const float length = std::hypot(desired.forward, desired.left);
    if (!std::isfinite(length))
        return {};
    if (speed < std::min(parameters.wishSpeed, parameters.airWishSpeedCap)) {
        // A nearly stationary player needs a useful launch direction rather than
        // a perpendicular impulse which abruptly sends them sideways.
        if (length > 0.001f)
            return {desired.forward / length, desired.left / length};
        return {1.0f, 0.0f};
    }

    const float velocityYaw = std::atan2(velocityY, velocityX);
    const float desiredOffset = length > 0.001f ? std::atan2(desired.left, desired.forward) : 0.0f;
    const float error = static_cast<float>(std::remainder(double(viewYaw) + desiredOffset - velocityYaw,
        double(trig::kTwoPi)));
    bool left;
    if (std::abs(error) > 0.5f * trig::kDegreesToRadians) {
        left = error > 0.0f;
    } else if (mouseDx != 0) {
        left = mouseDx < 0;
    } else {
        left = !alternateLeft;
    }
    alternateLeft = left;
    return moveAtAngle(velocityYaw, viewYaw, idealAngle(speed, parameters), left);
}

}
