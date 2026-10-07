#pragma once

#include <Config/ConfigVariable.h>
#include <Config/RangeConstrainedVariableParams.h>

namespace status_panel_vars
{




inline constexpr auto kOffsetRange = RangeConstrainedVariableParams<float>{.min = 0.0f, .max = 4096.0f, .def = 0.0f};
CONFIG_VARIABLE_RANGE(OffsetX, kOffsetRange);
CONFIG_VARIABLE_RANGE(OffsetY, kOffsetRange);
}
