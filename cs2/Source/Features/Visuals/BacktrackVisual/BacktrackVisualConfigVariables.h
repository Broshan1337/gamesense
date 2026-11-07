#pragma once

#include <Config/ConfigVariable.h>
#include "BacktrackVisualParams.h"

namespace backtrack_visual_vars
{

CONFIG_VARIABLE(Enabled, bool, false);
CONFIG_VARIABLE_RANGE(ColorMode, backtrack_visual_params::kColorMode);
CONFIG_VARIABLE(Color, color::Rgba, (color::Rgba{80, 255, 130, 230}));
CONFIG_VARIABLE(HideInSmoke, bool, true);
CONFIG_VARIABLE_RANGE(LingerMs, backtrack_visual_params::kLingerMs);

}
