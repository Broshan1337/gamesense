#pragma once

// Minimal trigonometry, implemented here rather than taken from libm.
//
// This project links -nostdlib and uses no math library anywhere else, so <cmath> is not an option.
// Only sine and cosine need real work: square root maps to a single SQRTSS instruction through
// __builtin_sqrtf and never calls into libm.
namespace trig
{

inline constexpr float kPi = 3.14159265358979323846f;
inline constexpr float kHalfPi = kPi * 0.5f;
inline constexpr float kTwoPi = kPi * 2.0f;
inline constexpr float kDegreesToRadians = kPi / 180.0f;
inline constexpr float kRadiansToDegrees = 180.0f / kPi;

[[nodiscard]] inline float squareRoot(float value) noexcept
{
    return value > 0.0f ? __builtin_sqrtf(value) : 0.0f;
}

[[nodiscard]] inline float absolute(float value) noexcept
{
    return value < 0.0f ? -value : value;
}

// Wraps an angle in radians into [-pi, pi]. Done with an integer round-trip rather than fmod, both
// to avoid libm and because the inputs here are small (view angles), so the cast cannot overflow.
[[nodiscard]] inline float wrapToPi(float radians) noexcept
{
    const float turns = radians * (1.0f / kTwoPi);
    const float rounded = static_cast<float>(static_cast<int>(turns >= 0.0f ? turns + 0.5f : turns - 0.5f));
    return radians - kTwoPi * rounded;
}

// Taylor series through x^9, evaluated after folding the argument into [-pi/2, pi/2] where that
// series is accurate to roughly 1e-7 - far tighter than anything steering movement needs.
[[nodiscard]] inline float sine(float radians) noexcept
{
    float x = wrapToPi(radians);

    if (x > kHalfPi)
        x = kPi - x;
    else if (x < -kHalfPi)
        x = -kPi - x;

    const float x2 = x * x;
    return x * (1.0f + x2 * (-0.16666667f + x2 * (0.0083333333f + x2 * (-0.00019841270f + x2 * 2.7557319e-6f))));
}

[[nodiscard]] inline float cosine(float radians) noexcept
{
    return sine(radians + kHalfPi);
}

// Normalizes a DEGREE angle into (-180, 180], the form yaw differences need before they can be
// compared against a dead-band.
[[nodiscard]] inline float normalizeDegrees(float degrees) noexcept
{
    return wrapToPi(degrees * kDegreesToRadians) * kRadiansToDegrees;
}

// atan2 in RADIANS, matching Source's convention (+X is angle 0, +Y is +pi/2). Implemented as a
// rational approximation rather than taken from libm.
[[nodiscard]] inline float arcTangent2(float deltaY, float deltaX) noexcept
{
    if (deltaX == 0.0f && deltaY == 0.0f)
        return 0.0f;

    const float absX = absolute(deltaX);
    const float absY = absolute(deltaY);

    // atan of the ratio of the smaller to the larger component, then reflected into the right
    // octant. Keeping the ratio <= 1 is what bounds the approximation's error.
    const float ratio = (absX >= absY) ? (absY / absX) : (absX / absY);
    const float r2 = ratio * ratio;

    // Minimax cubic-over-linear style polynomial for atan on [0, 1]; error stays under ~1e-4 rad,
    // which is a hundredth of the dead-band the blockbot compares against.
    float angle = ratio * (0.9998660f + r2 * (-0.3302995f + r2 * (0.1801410f + r2 * (-0.0851330f + r2 * 0.0208351f))));

    if (absY > absX)
        angle = kHalfPi - angle;
    if (deltaX < 0.0f)
        angle = kPi - angle;
    if (deltaY < 0.0f)
        angle = -angle;

    return angle;
}

// Yaw in DEGREES from one 2D point to another - the same angle, in the units view angles use.
[[nodiscard]] inline float yawTo(float deltaX, float deltaY) noexcept
{
    return arcTangent2(deltaY, deltaX) * kRadiansToDegrees;
}

// acos in RADIANS, built on the atan2 above via the identity acos(x) = atan2(sqrt(1 - x^2), x).
// That reuses one approximation instead of introducing a second, and is exact at the endpoints.
[[nodiscard]] inline float arcCosine(float value) noexcept
{
    if (value >= 1.0f)
        return 0.0f;
    if (value <= -1.0f)
        return kPi;

    return arcTangent2(squareRoot(1.0f - value * value), value);
}

// Same clamping discipline as arcCosine: SuperToss feeds this the corrected throw direction's
// z component, which can sit at exactly ±1 on a straight-down throw.
[[nodiscard]] inline float arcSine(float value) noexcept
{
    if (value >= 1.0f)
        return kHalfPi;
    if (value <= -1.0f)
        return -kHalfPi;

    return arcTangent2(value, squareRoot(1.0f - value * value));
}

}
