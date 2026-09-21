#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include <Hooks/Graphics/VulkanHook.h>

// CPU generation of the menu's soft-shadow stamp: a gaussian-blurred rounded box, uploaded once
// by the Vulkan hook (avatar-style staging) and sampled by the UI's 9-slice shadow drawing.
// Header-only so the math is unit-testable; VulkanHook.cpp only stages the bytes.
namespace VulkanHook::shadow_texture
{

// Closed form: convolving the rounded box's step edge with a gaussian has the exact erfc
// solution, so no iterative blur pass is needed and the stamp is bit-exact every session
// (~16k erfcf evaluations, once). sigma = margin/2 leaves ~2% alpha at the stamp border.
// Layout contract consumed by the UI (Neverlose.cpp softShadow): the box edge sits exactly at
// kMargin px inside the stamp on every side; outside it the alpha decays monotonically to ~0;
// the stamp is exactly symmetric on both axes, which is what makes the 9-slice seam-free.
inline void generateShadowStamp(std::uint8_t* pixels) noexcept
{
    constexpr float coreHalf = (kStampSize / 2) - kMargin;
    constexpr float radius = 28.0f; // stamp px (maps to s(14) on screen): matches card corners
    constexpr float sigmaSqrt2 = (kMargin / 2.0f) * 1.41421356f;
    for (int y = 0; y < kStampSize; ++y) {
        for (int x = 0; x < kStampSize; ++x) {
            const float px = (x + 0.5f) - kStampSize / 2.0f;
            const float py = (y + 0.5f) - kStampSize / 2.0f;
            const float qx = std::fabs(px) - (coreHalf - radius);
            const float qy = std::fabs(py) - (coreHalf - radius);
            const float ax = std::max(qx, 0.0f);
            const float ay = std::max(qy, 0.0f);
            const float dist = std::sqrt(ax * ax + ay * ay) + std::min(std::max(qx, qy), 0.0f) - radius;
            const float alpha = 0.5f * std::erfcf(dist / sigmaSqrt2);
            std::uint8_t* texel = pixels + 4u * (static_cast<std::size_t>(y) * kStampSize + x);
            texel[0] = 255;
            texel[1] = 255;
            texel[2] = 255;
            texel[3] = static_cast<std::uint8_t>(std::clamp(alpha, 0.0f, 1.0f) * 255.0f);
        }
    }
}

}
