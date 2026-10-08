#pragma once

#include <cmath>
#include <CS2/Classes/Vector.h>

namespace viewmodel_rotation {
// These angles belong to the HUD model's private pose, never ViewSetup or usercmd.
[[nodiscard]] inline cs2::Vector apply(cs2::Vector angles, float pitch, float roll) noexcept
{
    if (!std::isfinite(angles.x) || !std::isfinite(angles.y) || !std::isfinite(angles.z)
        || !std::isfinite(pitch) || !std::isfinite(roll))
        return angles;
    if (pitch != 0.0f)
        angles.x = std::remainder(angles.x + pitch, 360.0f);
    if (roll != 0.0f)
        angles.z = std::remainder(angles.z + roll, 360.0f);
    return angles;
}
}
