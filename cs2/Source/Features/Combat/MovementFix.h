#pragma once

#include <algorithm>
#include <cstring>

#include <GameClient/UserCmd.h>
#include <Utils/Optional.h>
#include <Utils/Trig.h>

















namespace movement_fix
{




inline void setViewAngles(const UserCmd& userCmd, float newPitch, float newYaw) noexcept
{
    const auto oldPitch = userCmd.viewPitch();
    const auto oldYaw = userCmd.viewYaw();
    const auto oldForward = userCmd.forwardMove();
    const auto oldLeft = userCmd.leftMove();
    if (!oldPitch.hasValue() || !oldYaw.hasValue() || !oldForward.hasValue() || !oldLeft.hasValue()) {
        userCmd.setViewAngles(newPitch, newYaw);
        return;
    }

    userCmd.setViewAngles(newPitch, newYaw);

    float delta = newYaw - oldYaw.value();
    while (delta > 180.0f)
        delta -= 360.0f;
    while (delta < -180.0f)
        delta += 360.0f;

    const float forward = oldForward.valueOr(0.0f);
    const float left = oldLeft.valueOr(0.0f);

    
    const float sign = newPitch > 89.0f ? -1.0f : 1.0f;

    const float radians = delta * (3.14159265358979f / 180.0f);
    
    
    const float c = trig::cosine(radians);
    const float s = trig::sine(radians);

    const float fixedForward = sign * (c * forward + s * left);
    const float fixedLeft = c * left - s * forward;

    userCmd.setForwardMove(std::clamp(fixedForward, -1.0f, 1.0f));
    userCmd.setLeftMove(std::clamp(fixedLeft, -1.0f, 1.0f));
}

}