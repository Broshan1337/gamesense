#pragma once

#include <CS2/Classes/Vector.h>
#include <Utils/Trig.h>




namespace shot_geometry
{

struct Basis {
    cs2::Vector forward;
    cs2::Vector left;
    cs2::Vector up;
};

struct Angles {
    float pitch;
    float yaw;
};



[[nodiscard]] inline Angles anglesTo(const cs2::Vector& from, const cs2::Vector& to) noexcept
{
    const auto dx = to.x - from.x;
    const auto dy = to.y - from.y;
    const auto dz = to.z - from.z;
    const auto horizontal = trig::squareRoot(dx * dx + dy * dy);
    return Angles{
        trig::arcTangent2(-dz, horizontal) * trig::kRadiansToDegrees,
        trig::arcTangent2(dy, dx) * trig::kRadiansToDegrees,
    };
}




[[nodiscard]] inline Basis angleVectors(float pitchDegrees, float yawDegrees, float rollDegrees = 0.0f) noexcept
{
    const auto pitch = pitchDegrees * trig::kDegreesToRadians;
    const auto yaw = yawDegrees * trig::kDegreesToRadians;
    const auto roll = rollDegrees * trig::kDegreesToRadians;

    const auto sinPitch = trig::sine(pitch);
    const auto cosPitch = trig::cosine(pitch);
    const auto sinYaw = trig::sine(yaw);
    const auto cosYaw = trig::cosine(yaw);
    const auto sinRoll = trig::sine(roll);
    const auto cosRoll = trig::cosine(roll);

    Basis basis;
    basis.forward = {cosPitch * cosYaw, cosPitch * sinYaw, -sinPitch};
    basis.left = {-sinRoll * sinPitch * cosYaw + cosRoll * sinYaw, -sinRoll * sinPitch * sinYaw - cosRoll * cosYaw, -sinRoll * cosPitch};
    basis.up = {cosRoll * sinPitch * cosYaw + sinRoll * sinYaw, cosRoll * sinPitch * sinYaw - sinRoll * cosYaw, cosRoll * cosPitch};
    return basis;
}

[[nodiscard]] inline cs2::Vector normalized(const cs2::Vector& v) noexcept
{
    const auto length = trig::squareRoot(v.x * v.x + v.y * v.y + v.z * v.z);
    if (length <= 0.0f)
        return v;
    const auto inverse = 1.0f / length;
    return {v.x * inverse, v.y * inverse, v.z * inverse};
}




[[nodiscard]] inline bool rayReachesSphere(const cs2::Vector& eye, const cs2::Vector& direction, const cs2::Vector& center, float radius) noexcept
{
    const cs2::Vector toCenter{center.x - eye.x, center.y - eye.y, center.z - eye.z};
    const auto along = toCenter.x * direction.x + toCenter.y * direction.y + toCenter.z * direction.z;
    if (along <= 0.0f)
        return false;

    const cs2::Vector closest{eye.x + direction.x * along, eye.y + direction.y * along, eye.z + direction.z * along};
    const auto dx = center.x - closest.x;
    const auto dy = center.y - closest.y;
    const auto dz = center.z - closest.z;
    return (dx * dx + dy * dy + dz * dz) <= radius * radius;
}

}
