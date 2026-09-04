#pragma once

#include <Config/ConfigVariable.h>
#include <Config/RangeConstrainedVariableParams.h>

CONFIG_VARIABLE(PlayerListEnabled, bool, false);

namespace player_list_params
{
// Window offsets from the top-left anchor (the watermark's slider pattern: the window is
// PINNED to anchor + offset every frame, so the sliders and the config are the single source
// of position truth - no dragging).
// Float range so the window can live anywhere on modern screens (3440x1440+).
inline constexpr auto kOffsetRange = RangeConstrainedVariableParams<float>{.min = 0.0f, .max = 4096.0f, .def = 10.0f};
}

CONFIG_VARIABLE_RANGE(PlayerListOffsetX, player_list_params::kOffsetRange);
CONFIG_VARIABLE_RANGE(PlayerListOffsetY, player_list_params::kOffsetRange);
