#pragma once

#include <atomic>
#include <cstdint>

// Live menu-accent RGB shared between the two threads that render UI: refreshMenuTheme
// (present thread, Neverlose.cpp) publishes the configured accent every frame - including the
// fading-RGB style, so rainbow mode flows through too - and game-thread panorama features
// (HudThemeColor) read it when they re-apply their colors. 0x00RRGGBB.
namespace theme_accent
{

inline std::atomic<std::uint32_t> current{0xA3D41F}; // legacy watermark green until the menu publishes

inline void publish(std::uint8_t r, std::uint8_t g, std::uint8_t b) noexcept
{
    current.store(static_cast<std::uint32_t>(r) << 16 | static_cast<std::uint32_t>(g) << 8 | b,
        std::memory_order_release);
}

[[nodiscard]] inline std::uint32_t rgb() noexcept
{
    return current.load(std::memory_order_acquire);
}

}