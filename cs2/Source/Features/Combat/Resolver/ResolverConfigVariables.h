#pragma once

#include <Config/ConfigVariable.h>
#include <Config/RangeConstrainedVariableParams.h>

namespace resolver_params
{
inline constexpr auto kMissTicks = RangeConstrainedVariableParams<std::uint8_t>{.min = 1, .max = 10, .def = 3};
}

namespace resolver_vars
{
CONFIG_VARIABLE(Enabled, bool, true);
CONFIG_VARIABLE_RANGE(MissTicks, resolver_params::kMissTicks);
CONFIG_VARIABLE(ResetOnRound, bool, true);
}