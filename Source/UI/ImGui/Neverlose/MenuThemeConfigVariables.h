#pragma once

#include <cstdint>

#include <Config/ConfigVariable.h>
#include <Config/RangeConstrainedVariableParams.h>
#include <Features/Hud/Watermark/WatermarkPanelParams.h>

// Menu theme colors, editable in the profile popover with the RGBA channel picker. Defaults all
// derive from the HUD watermark's text color (the user's picked green), readability-clamped the
// same way the old compile-time accent was; changing the picker writes config, so the theme
// persists.
namespace menu_theme_vars
{

// Rec. 709 luminance, 0..1, plus the readable-on-dark clamp: too dark for the dark shell ->
// lift toward white; near-white -> pulled back toward the shell so it stops glaring.
constexpr float kMinReadableLuma = 0.42f;

constexpr std::uint8_t lerpChannel(std::uint8_t c, std::uint8_t target, float t) noexcept
{
    return static_cast<std::uint8_t>(c + (static_cast<float>(target) - c) * t + 0.5f);
}

constexpr color::Rgba readableOnDark(color::Rgba color) noexcept
{
    const float luma = (0.2126f * color.r() + 0.7152f * color.g() + 0.0722f * color.b()) / 255.0f;
    if (luma < kMinReadableLuma) {
        const float t = (kMinReadableLuma - luma) / (1.0f - luma);
        const auto mix = [t](std::uint8_t c) { return static_cast<std::uint8_t>(c + (255 - c) * t + 0.5f); };
        return color::Rgba{mix(color.r()), mix(color.g()), mix(color.b()), color.a()};
    }
    return color;
}

inline constexpr color::Rgba kDefaultAccent{0xA3, 0xD4, 0x1F, 255}; // the watermark's green
inline constexpr auto kDefaultButtonColor = color::Rgba{0xA3, 0xD4, 0x1F, 255};
inline constexpr auto kDefaultSliderColor = color::Rgba{0xA3, 0xD4, 0x1F, 255};

// Outer menu glow: MenuGlowColor tints the gaussian stamp around the shell (its alpha channel is
// the glow strength); MenuGlowRainbow ignores the RGB channels and cycles hue over time at
// MenuGlowSpeed (full rainbow cycle = 10 / speed seconds); alpha still applies in rainbow mode.
inline constexpr auto kDefaultGlowColor = color::Rgba{0xA3, 0xD4, 0x1F, 220};
inline constexpr auto kGlowSpeed = RangeConstrainedVariableParams<float>{.min = 0.2f, .max = 10.0f, .def = 2.0f};
// 15 keeps the stamp's corner curve at the shell's own s(17) rounding (the stamp maps its
// 24px margin band 1:1 at s(12); curve radius on screen = 1.1667 x margin). Bigger sizes stretch
// the corner radius proportionally - inherent to the 9-slice.
inline constexpr auto kGlowSize = RangeConstrainedVariableParams<float>{.min = 6.0f, .max = 60.0f, .def = 15.0f};

}

CONFIG_VARIABLE(MenuAccentColor, color::Rgba, (menu_theme_vars::readableOnDark(menu_theme_vars::kDefaultAccent)));
CONFIG_VARIABLE(MenuButtonColor, color::Rgba, (menu_theme_vars::kDefaultButtonColor));
CONFIG_VARIABLE(MenuSliderColor, color::Rgba, (menu_theme_vars::kDefaultSliderColor));
CONFIG_VARIABLE(MenuGlowEnabled, bool, true);
CONFIG_VARIABLE(MenuGlowColor, color::Rgba, (menu_theme_vars::kDefaultGlowColor));
CONFIG_VARIABLE(MenuGlowRainbow, bool, false);
CONFIG_VARIABLE_RANGE(MenuGlowSpeed, menu_theme_vars::kGlowSpeed);
CONFIG_VARIABLE_RANGE(MenuGlowSize, menu_theme_vars::kGlowSize);
CONFIG_VARIABLE(MenuGlowDebug, bool, false);
