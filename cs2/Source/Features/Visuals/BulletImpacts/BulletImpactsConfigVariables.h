#pragma once

#include <Config/ConfigVariable.h>
#include <Config/RangeConstrainedVariableParams.h>
#include <Utils/ColorUtils.h>

namespace bullet_impacts_vars
{

CONFIG_VARIABLE(Enabled, bool, false);
CONFIG_VARIABLE(Color, color::Rgba, (color::Rgba{255, 128, 0, 220}));

// Marker lifetime in centiseconds (0.25 s .. 1.0 s).
inline constexpr auto kLifetimeRange = RangeConstrainedVariableParams<std::uint8_t>{.min = 25, .max = 100, .def = 40};
CONFIG_VARIABLE_RANGE(Lifetime, kLifetimeRange);

}