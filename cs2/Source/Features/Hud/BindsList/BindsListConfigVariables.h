#pragma once

#include <Config/ConfigVariable.h>
#include <Config/RangeConstrainedVariableParams.h>

namespace binds_list_vars
{
// The in-game keybind list overlay (player-list style HUD panel listing every bind with its
// key; the key pill glows while the key is physically held). Off by default.
CONFIG_VARIABLE(Enabled, bool, false);

namespace params
{
// Position offsets from the list's default top-right anchor in screen pixels: OffsetX moves the
// window LEFT of the anchor, OffsetY moves it DOWN from the anchor (drag direction). Negative
// values go the opposite way (above/right of the anchor) - 0 is NOT a barrier, it is just the
// default anchor. Float range so the window can live anywhere on modern screens; the mouse
// drag writes the same offsets the sliders would, so both are position truth.
inline constexpr auto kOffsetRange = RangeConstrainedVariableParams<float>{.min = -4096.0f, .max = 4096.0f, .def = 0.0f};
}

CONFIG_VARIABLE_RANGE(OffsetX, params::kOffsetRange);
CONFIG_VARIABLE_RANGE(OffsetY, params::kOffsetRange);
}
