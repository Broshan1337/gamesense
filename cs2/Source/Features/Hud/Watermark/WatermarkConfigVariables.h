#pragma once

#include <Config/ConfigVariable.h>
#include <Config/RangeConstrainedVariableParams.h>

namespace watermark_vars
{

CONFIG_VARIABLE(Enabled, bool, true);



CONFIG_VARIABLE(ShowFps, bool, true);
CONFIG_VARIABLE(ShowSpeed, bool, true);
CONFIG_VARIABLE(ShowPing, bool, true);
CONFIG_VARIABLE(ShowTeamDamage, bool, true);
CONFIG_VARIABLE(ShowClock, bool, true);



inline constexpr auto kOffsetRange = RangeConstrainedVariableParams<std::uint8_t>{.min = 0, .max = 200, .def = 10};
CONFIG_VARIABLE_RANGE(OffsetX, kOffsetRange);
CONFIG_VARIABLE_RANGE(OffsetY, kOffsetRange);
}
