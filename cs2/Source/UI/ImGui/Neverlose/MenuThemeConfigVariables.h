#pragma once

#include <cstdint>

#include <Config/ConfigVariable.h>
#include <Config/RangeConstrainedVariableParams.h>
#include <Features/Hud/Watermark/WatermarkPanelParams.h>





namespace menu_theme_vars
{



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



inline constexpr color::Rgba kDefaultAccent{150, 127, 238, 255};
inline constexpr auto kDefaultButtonColor = color::Rgba{150, 127, 238, 255};
inline constexpr auto kDefaultSliderColor = color::Rgba{150, 127, 238, 255};




inline constexpr auto kDefaultGlowColor = color::Rgba{150, 127, 235, 220};
inline constexpr auto kGlowSpeed = RangeConstrainedVariableParams<float>{.min = 0.2f, .max = 10.0f, .def = 2.0f};



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




CONFIG_VARIABLE(MenuStyleRainbow, bool, false);



CONFIG_VARIABLE(MenuReduceMotion, bool, false);
