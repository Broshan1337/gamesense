#pragma once

#include <Config/ConfigVariable.h>
#include <Utils/ColorUtils.h>

namespace spread_circle_vars
{

CONFIG_VARIABLE(Enabled, bool, false);

// Custom circle color. When the alpha is 0 the visual falls back to the in-game crosshair
// color (the original behavior) - the menu's picker row labels that state "Crosshair".
CONFIG_VARIABLE(SpreadCircleColor, color::Rgba, (color::Rgba{255, 255, 255, 0}));

}
