#pragma once

#include <Config/ConfigVariable.h>
#include <Config/RangeConstrainedVariableParams.h>

namespace watermark_vars
{
// The "Neversneeze | 84 | fps" HUD watermark. On by default; it is cosmetic and local-only.
CONFIG_VARIABLE(Enabled, bool, true);

// Segment toggles - each "| fps", "| u/s", "| ping", "| td" and clock chunk of the watermark
// can be hidden individually (appended LAST to the config schema, see ConfigVariableTypes.h).
CONFIG_VARIABLE(ShowFps, bool, true);
CONFIG_VARIABLE(ShowSpeed, bool, true);
CONFIG_VARIABLE(ShowPing, bool, true);
CONFIG_VARIABLE(ShowTeamDamage, bool, true);
CONFIG_VARIABLE(ShowClock, bool, true);

// Box offset from the top-right corner of the screen in HUD pixels (default = the original
// kBoxMargin). The Hud page exposes them as sliders. uint8: the schema loader's range types.
inline constexpr auto kOffsetRange = RangeConstrainedVariableParams<std::uint8_t>{.min = 0, .max = 200, .def = 10};
CONFIG_VARIABLE_RANGE(OffsetX, kOffsetRange);
CONFIG_VARIABLE_RANGE(OffsetY, kOffsetRange);
}
