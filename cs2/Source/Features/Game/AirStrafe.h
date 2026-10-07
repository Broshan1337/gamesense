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
    const float acceleration = parameters.wishSpeed * parameters.airAccelerate
        * parameters.frameTime * parameters.surfaceFriction;
    const float projection = std::max(0.0f, cap - acceleration);
    return trig::arcCosine(std::clamp(projection / speed, 0.0f, 1.0f));
}

[[nodiscard]] inline Move moveAtAngle(float velocityYaw, float viewYaw, float angle, bool left) noexcept
{
    const float direction = trig::wrapToPi(velocityYaw - viewYaw + (left ? angle : -angle));
    const float forward = trig::cosine(direction);
    const float side = trig::sine(direction);
    // Polynomial trig leaves small residuals at cardinal angles. Do not turn
    // those residuals into an unintended forward/back or left/right button.
    return {trig::absolute(forward) < 0.0001f ? 0.0f : forward,
        trig::absolute(side) < 0.0001f ? 0.0f : side};
}

// Prefer the optimal branch that turns toward the requested movement direction.
// Mouse turning takes priority; alternating breaks ties without a fixed side bias.
[[nodiscard]] inline Move steer(float velocityX, float velocityY, float viewYaw,
    Move desired, int mouseDx, bool& alternateLeft, const Parameters& parameters) noexcept
{
    const float speed = trig::squareRoot(velocityX * velocityX + velocityY * velocityY);
    if (speed < 1.0f) {
        const float length = trig::squareRoot(desired.forward * desired.forward + desired.left * desired.left);
        if (length > 0.0f)
            return {desired.forward / length, desired.left / length};
        return {1.0f, 0.0f};
    }

    const float velocityYaw = trig::arcTangent2(velocityY, velocityX);
    bool left;
    if (mouseDx != 0) {
        left = mouseDx < 0;
    } else {
        const float desiredYaw = viewYaw + trig::arcTangent2(desired.left, desired.forward);
        const float error = trig::wrapToPi(desiredYaw - velocityYaw);
        if (trig::absolute(error) > 0.5f * trig::kDegreesToRadians)
            left = error > 0.0f;
        else
            left = !alternateLeft;
    }
    alternateLeft = left;
    return moveAtAngle(velocityYaw, viewYaw, idealAngle(speed, parameters), left);
}

}
