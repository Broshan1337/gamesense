#pragma once

#include <Config/RangeConstrainedVariableParams.h>

namespace third_person_vars
{
inline constexpr auto kDistance = RangeConstrainedVariableParams<float>{.min = 0.0f, .max = 300.0f, .def = 150.0f};
}

CONFIG_VARIABLE(ForceThirdPersonEnabled, bool, false);
CONFIG_VARIABLE_RANGE(ForceThirdPersonDistance, third_person_vars::kDistance);
