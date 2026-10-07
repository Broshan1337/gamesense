#pragma once

#include <cmath>
#include <cstdint>
#include <CS2/Classes/Vector.h>
#include <Utils/Trig.h>

namespace spread_model {
// Statistical cone model, deliberately independent of the engine's shot seed/ABI.
// Uniform disk area (sqrt radius) is conservative relative to center-weighted
// cones. This estimates single-projectile probability, never exact seed behavior.
inline bool valid(float inaccuracy, float spread) noexcept
{
    return std::isfinite(inaccuracy) && std::isfinite(spread)
        && inaccuracy >= 0.0f && spread >= 0.0f && inaccuracy + spread <= 4.0f;
}

inline cs2::Vector sample(std::uint32_t index, float inaccuracy, float spread) noexcept
{
    std::uint32_t state = index ^ 0xA511E9B3u;
    const auto unit = [&]() {
        state += 0x9E3779B9u;
        auto x = state;
        x = (x ^ (x >> 16)) * 0x85EBCA6Bu;
        x = (x ^ (x >> 13)) * 0xC2B2AE35u;
        return static_cast<float>((x ^ (x >> 16)) >> 8) / 16777216.0f;
    };
    constexpr float tau = 6.283185307179586f;
    const float r1 = trig::squareRoot(unit()) * inaccuracy;
    const float a1 = unit() * tau;
    const float r2 = trig::squareRoot(unit()) * spread;
    const float a2 = unit() * tau;
    return {trig::cosine(a1) * r1 + trig::cosine(a2) * r2,
            trig::sine(a1) * r1 + trig::sine(a2) * r2, 0.0f};
}
}
