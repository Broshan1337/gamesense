#pragma once

#include <Config/ConfigVariable.h>
#include <Utils/ColorUtils.h>

namespace spread_circle_vars
{

CONFIG_VARIABLE(Enabled, bool, false);



CONFIG_VARIABLE(SpreadCircleColor, color::Rgba, (color::Rgba{255, 255, 255, 0}));

}
