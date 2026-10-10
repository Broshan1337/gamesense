#pragma once

#include <Config/ConfigVariable.h>
#include <Config/RangeConstrainedVariableParams.h>

namespace off_screen_arrows_params
{
inline constexpr auto kRadius = RangeConstrainedVariableParams<float>{.min = 10.0f, .max = 1000.0f, .def = 150.0f};
inline constexpr auto kSize = RangeConstrainedVariableParams<float>{.min = 4.0f, .max = 80.0f, .def = 16.0f};
}

CONFIG_VARIABLE(OffScreenArrowsEnabled, bool, false);
CONFIG_VARIABLE(OffScreenArrowsColor, color::Rgba, (color::Rgba{255, 90, 90, 230}));
CONFIG_VARIABLE_RANGE(OffScreenArrowsRadius, off_screen_arrows_params::kRadius);
CONFIG_VARIABLE_RANGE(OffScreenArrowsSize, off_screen_arrows_params::kSize);