#pragma once

#include <cstdint>
#include <cstring>






namespace fastmath {

[[nodiscard]] inline float log2f(float x) noexcept
{
    std::uint32_t bits;
    std::memcpy(&bits, &x, sizeof(bits));
    const std::uint32_t mbits = (bits & 0x007FFFFFu) | 0x3f000000u;
    float m;
    std::memcpy(&m, &mbits, sizeof(m));
    const float y = static_cast<float>(bits) * 1.1920928955078125e-7f;
    return y - 124.22551499f - 1.498030302f * m - 1.72587999f / (0.3520887068f + m);
}

[[nodiscard]] inline float exp2f(float p) noexcept
{
    const float offset = (p < 0.0f) ? 1.0f : 0.0f;
    const float clipp = (p < -126.0f) ? -126.0f : p;
    const int w = static_cast<int>(clipp);
    const float z = clipp - static_cast<float>(w) + offset;
    const std::uint32_t bits = static_cast<std::uint32_t>((1u << 23) * (clipp + 121.2740575f + 27.7280233f / (4.84252568f - z) - 1.49012907f * z));
    float result;
    std::memcpy(&result, &bits, sizeof(result));
    return result;
}


[[nodiscard]] inline float powf(float x, float p) noexcept
{
    if (x <= 0.0f)
        return 0.0f;
    return exp2f(p * log2f(x));
}

} 
