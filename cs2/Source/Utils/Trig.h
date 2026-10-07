#pragma once






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



[[nodiscard]] inline float wrapToPi(float radians) noexcept
{
    const float turns = radians * (1.0f / kTwoPi);
    const float rounded = static_cast<float>(static_cast<int>(turns >= 0.0f ? turns + 0.5f : turns - 0.5f));
    return radians - kTwoPi * rounded;
}



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



[[nodiscard]] inline float normalizeDegrees(float degrees) noexcept
{
    return wrapToPi(degrees * kDegreesToRadians) * kRadiansToDegrees;
}



[[nodiscard]] inline float arcTangent2(float deltaY, float deltaX) noexcept
{
    if (deltaX == 0.0f && deltaY == 0.0f)
        return 0.0f;

    const float absX = absolute(deltaX);
    const float absY = absolute(deltaY);

    
    
    const float ratio = (absX >= absY) ? (absY / absX) : (absX / absY);
    const float r2 = ratio * ratio;

    
    
    float angle = ratio * (0.9998660f + r2 * (-0.3302995f + r2 * (0.1801410f + r2 * (-0.0851330f + r2 * 0.0208351f))));

    if (absY > absX)
        angle = kHalfPi - angle;
    if (deltaX < 0.0f)
        angle = kPi - angle;
    if (deltaY < 0.0f)
        angle = -angle;

    return angle;
}


[[nodiscard]] inline float yawTo(float deltaX, float deltaY) noexcept
{
    return arcTangent2(deltaY, deltaX) * kRadiansToDegrees;
}



[[nodiscard]] inline float arcCosine(float value) noexcept
{
    if (value >= 1.0f)
        return 0.0f;
    if (value <= -1.0f)
        return kPi;

    return arcTangent2(squareRoot(1.0f - value * value), value);
}



[[nodiscard]] inline float arcSine(float value) noexcept
{
    if (value >= 1.0f)
        return kHalfPi;
    if (value <= -1.0f)
        return -kHalfPi;

    return arcTangent2(value, squareRoot(1.0f - value * value));
}

}
