#pragma once

#include <Config/RangeConstrainedVariableParams.h>
#include <CS2/Classes/Color.h>
#include <Utils/ColorUtils.h>

namespace outline_glow_params
{
    constexpr auto kDefuseKitGlowRange = 800;
    constexpr auto kWeaponGlowRange = 800;

    constexpr auto kGlowAlpha = 102;
    constexpr auto kImmunePlayerGlowAlpha = 40;
    constexpr color::Saturation kSaturation{0.5f};
    constexpr color::Brightness kBrightness{1.0f};

    constexpr HueVariableParams kHostageHue{.min{0}, .max{359}, .def{50}};

    constexpr HueVariableParams kMolotovHue{.min{20}, .max{60}, .def{40}};
    constexpr HueVariableParams kFlashbangHue{.min{191}, .max{250}, .def{219}};
    constexpr HueVariableParams kHEGrenadeHue{.min{300}, .max{359}, .def{359}};
    constexpr HueVariableParams kSmokeGrenadeHue{.min{110}, .max{140}, .def{120}};

    constexpr HueVariableParams kDroppedBombHue{.min{0}, .max{359}, .def{60}};
    constexpr HueVariableParams kTickingBombHue{.min{0}, .max{359}, .def{0}};
    constexpr HueVariableParams kDefuseKitHue{.min{0}, .max{359}, .def{184}};

    constexpr cs2::Color kFallbackColor{191, 191, 191, kGlowAlpha};
}
