#pragma once

#include <Config/ConfigVariable.h>
#include <Config/RangeConstrainedVariableParams.h>

namespace status_panel_vars
{
// Status chips (AIM / TRIG / BLOCK): X/Y position offsets from the default bottom-left anchor
// (above the money/chat HUD) in HUD pixels. Float range so the box can live anywhere on modern
// screens (3440x1440+) - the box is mouse-draggable in game and the drag writes the same
// offsets, so the sliders and the drag are both position truth (the player list's pattern).
inline constexpr auto kOffsetRange = RangeConstrainedVariableParams<float>{.min = 0.0f, .max = 4096.0f, .def = 0.0f};
CONFIG_VARIABLE_RANGE(OffsetX, kOffsetRange);
CONFIG_VARIABLE_RANGE(OffsetY, kOffsetRange);
}
