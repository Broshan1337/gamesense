#pragma once

#include <Config/ConfigVariable.h>
#include <Config/RangeConstrainedVariableParams.h>

CONFIG_VARIABLE(PlayerListEnabled, bool, false);

namespace player_list_params
{




inline constexpr auto kOffsetRange = RangeConstrainedVariableParams<float>{.min = 0.0f, .max = 4096.0f, .def = 10.0f};
}

CONFIG_VARIABLE_RANGE(PlayerListOffsetX, player_list_params::kOffsetRange);
CONFIG_VARIABLE_RANGE(PlayerListOffsetY, player_list_params::kOffsetRange);
