#pragma once

#include <Config/ConfigVariable.h>
#include <Config/RangeConstrainedVariableParams.h>

CONFIG_VARIABLE(PlayerListEnabled, bool, false);

namespace player_list_params
{
// Window offsets from the top-left anchor (the watermark's slider pattern: the window is
// PINNED to anchor + offset every frame, so the sliders and the config are the single source
// of position truth - no dragging).
inline constexpr auto kOffsetRange = RangeConstrainedVariableParams<std::uint8_t>{.min = 0, .max = 200, .def = 10};
}

CONFIG_VARIABLE_RANGE(PlayerListOffsetX, player_list_params::kOffsetRange);
CONFIG_VARIABLE_RANGE(PlayerListOffsetY, player_list_params::kOffsetRange);
