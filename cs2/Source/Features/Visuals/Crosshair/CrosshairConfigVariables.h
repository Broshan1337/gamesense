#pragma once

#include <Config/ConfigVariable.h>
#include <Config/RangeConstrainedVariableParams.h>
#include <Utils/ColorUtils.h>

namespace crosshair_params
{
inline constexpr auto kLength = RangeConstrainedVariableParams<std::uint8_t>{.min = 0, .max = 100, .def = 5};
inline constexpr auto kThickness = RangeConstrainedVariableParams<std::uint8_t>{.min = 1, .max = 10, .def = 2};
inline constexpr auto kGap = RangeConstrainedVariableParams<std::uint8_t>{.min = 0, .max = 100, .def = 3};
inline constexpr auto kMinDamage = RangeConstrainedVariableParams<std::uint8_t>{.min = 0, .max = 200, .def = 60};
}

namespace crosshair_vars
{
CONFIG_VARIABLE(Enabled, bool, false);
CONFIG_VARIABLE(Dot, bool, true);
CONFIG_VARIABLE(Lines, bool, true);
CONFIG_VARIABLE_RANGE(Length, crosshair_params::kLength);
CONFIG_VARIABLE_RANGE(Thickness, crosshair_params::kThickness);
CONFIG_VARIABLE_RANGE(Gap, crosshair_params::kGap);
CONFIG_VARIABLE(Color, color::Rgba, (color::Rgba{255, 90, 90, 255}));
CONFIG_VARIABLE(Outline, bool, true);
CONFIG_VARIABLE(TStyle, bool, false);
CONFIG_VARIABLE(PenetrationEnabled, bool, true);
CONFIG_VARIABLE_RANGE(PenetrationMinDamage, crosshair_params::kMinDamage);
CONFIG_VARIABLE(PenetrationColorYes, color::Rgba, (color::Rgba{130, 255, 130, 255}));
CONFIG_VARIABLE(PenetrationColorNo, color::Rgba, (color::Rgba{255, 100, 100, 255}));
}
