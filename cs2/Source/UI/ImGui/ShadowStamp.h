#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include <Hooks/Graphics/VulkanHook.h>




namespace VulkanHook::shadow_texture
{







inline void generateShadowStamp(std::uint8_t* pixels) noexcept
{
    constexpr float coreHalf = (kStampSize / 2) - kMargin;
    constexpr float radius = 28.0f; 
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
