#pragma once

#include <Config/RangeConstrainedVariableParams.h>
#include <CS2/Classes/Color.h>
#include <CS2/Constants/ColorConstants.h>
#include <Utils/ColorUtils.h>

namespace model_glow_params
{
    constexpr cs2::Color kFallbackColor{cs2::kColorWhite};

    constexpr HueVariableParams kMolotovHue{.min{20}, .max{60}, .def{40}};
    constexpr HueVariableParams kFlashbangHue{.min{191}, .max{250}, .def{219}};
    constexpr HueVariableParams kHEGrenadeHue{.min{300}, .max{359}, .def{359}};
    constexpr HueVariableParams kSmokeGrenadeHue{.min{110}, .max{140}, .def{120}};

    constexpr HueVariableParams kDroppedBombHue{.min{0}, .max{359}, .def{60}};
    constexpr HueVariableParams kTickingBombHue{.min{0}, .max{359}, .def{0}};
    constexpr HueVariableParams kDefuseKitHue{.min{0}, .max{359}, .def{184}};
}
