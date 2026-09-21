#pragma once

#include <algorithm>
#include <cstring>

#include <GameClient/UserCmd.h>
#include <Utils/Optional.h>
#include <Utils/Trig.h>

// Friend-source CMovement::MovementFix: after a feature overwrites the command's view angles,
// the analog movement components must be re-expressed or every strafe rotates with the aim.
//
// The server air-accelerates against the wish direction derived from the command's angles and
// the raw forward/left pair. Those pair components were computed against the PRE-fix angles
// (the view the player actually held when slot 6 built the command), so changing the angles
// later silently rotates the movement. The fix expresses the old movement in world space and
// projects it back onto the new basis - mathematically a 2D rotation by the yaw delta:
//
//   fixedForward =  cos(delta) * forward + sin(delta) * left
//   fixedLeft    = -sin(delta) * forward + cos(delta) * left
//
// with delta = newYaw - oldYaw, plus the inverted-view sign flip the reference carries for a
// pitch past 89 degrees (a clamped pitch can never legally sit there, but the flip costs one
// compare and matches the reference exactly). CS2's base message carries no meaningful upmove
// contribution on the ground, so forward/left alone is the complete projection here.
namespace movement_fix
{

// Writes the new angles AND the re-projected analog pair. All reads (old angles, raw
// components) happen BEFORE the angle write, so chained callers (RCS then the shot writer)
// compose correctly: each one fixes against whatever the command currently carries.
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

    // The friend-source sign convention: past 89 degrees of pitch the forward basis inverts.
    const float sign = newPitch > 89.0f ? -1.0f : 1.0f;

    const float radians = delta * (3.14159265358979f / 180.0f);
    // trig:: cosine/sine are the tree's libm-free helpers; the plain formula below only needs
    // them once each.
    const float c = trig::cosine(radians);
    const float s = trig::sine(radians);

    const float fixedForward = sign * (c * forward + s * left);
    const float fixedLeft = c * left - s * forward;

    userCmd.setForwardMove(std::clamp(fixedForward, -1.0f, 1.0f));
    userCmd.setLeftMove(std::clamp(fixedLeft, -1.0f, 1.0f));
}

}