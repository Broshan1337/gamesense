#pragma once

#include <Config/RangeConstrainedVariableParams.h>

namespace hitmarker_vars
{
inline constexpr auto kLength = RangeConstrainedVariableParams<float>{.min = 0.0f, .max = 30.0f, .def = 6.0f};
inline constexpr auto kGap = RangeConstrainedVariableParams<float>{.min = 0.0f, .max = 30.0f, .def = 4.0f};
inline constexpr auto kTimeout = RangeConstrainedVariableParams<float>{.min = 0.0f, .max = 2000.0f, .def = 500.0f};
}

CONFIG_VARIABLE(HitmarkerEnabled, bool, false);
CONFIG_VARIABLE_RANGE(HitmarkerLength, hitmarker_vars::kLength);
CONFIG_VARIABLE_RANGE(HitmarkerGap, hitmarker_vars::kGap);
CONFIG_VARIABLE_RANGE(HitmarkerTimeout, hitmarker_vars::kTimeout);
CONFIG_VARIABLE(HitmarkerColor, color::Rgba, (color::Rgba{255, 64, 64, 255}));
