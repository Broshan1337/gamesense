#pragma once

#include <Config/ConfigVariable.h>
#include <Config/RangeConstrainedVariableParams.h>

namespace binds_list_vars
{


CONFIG_VARIABLE(Enabled, bool, false);

namespace params
{





inline constexpr auto kOffsetRange = RangeConstrainedVariableParams<float>{.min = -4096.0f, .max = 4096.0f, .def = 0.0f};
}

CONFIG_VARIABLE_RANGE(OffsetX, params::kOffsetRange);
CONFIG_VARIABLE_RANGE(OffsetY, params::kOffsetRange);
}
