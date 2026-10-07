#pragma once















namespace fva_math
{

inline constexpr float kFullCircle = 360.0f;
inline constexpr float kHalfCircle = 180.0f;




inline constexpr float kWhenBase = 0.023590f;
inline constexpr float kWhenSpan = 0.908453f;







[[nodiscard]] inline float fold180(float delta) noexcept
{
    if (delta != delta) 
        return 0.0f;

    auto v = delta - kFullCircle * static_cast<float>(static_cast<int>(delta / kFullCircle));
    if (v > kHalfCircle)
        v -= kFullCircle;
    else if (v < -kHalfCircle)
        v += kFullCircle;

    if (v == -kHalfCircle)
        v = kHalfCircle;
    return v;
}







[[nodiscard]] inline bool isDiscontinuity(float previousAngle, float currentAngle) noexcept
{
    if (previousAngle != previousAngle || currentAngle != currentAngle)
        return true;
    const float d = fold180(currentAngle - previousAngle);
    return !(d < kHalfCircle && d > -kHalfCircle);
}




[[nodiscard]] inline int plannedEntries(int requested, int freeSlots) noexcept
{
    if (requested <= 0 || freeSlots <= 0)
        return 0;
    return requested < freeSlots ? requested : freeSlots;
}






[[nodiscard]] inline float interpolated(float from, float to, int index, int count) noexcept
{
    if (count <= 0 || index >= count)
        return to;
    if (index < 0)
        index = 0;
    const float fraction = static_cast<float>(index + 1) / static_cast<float>(count);
    return from + fraction * fold180(to - from);
}



[[nodiscard]] inline float whenAt(int index, int count) noexcept
{
    if (count <= 0)
        return kWhenBase;
    if (index >= count)
        index = count - 1;
    if (index < 0)
        index = 0;
    const float fraction = static_cast<float>(index + 1) / static_cast<float>(count);
    return kWhenBase + fraction * kWhenSpan;
}


[[nodiscard]] inline float foldedMagnitude(float delta) noexcept
{
    const float folded = fold180(delta);
    return folded < 0.0f ? -folded : folded;
}

} 
