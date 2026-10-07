#pragma once

#include <Config/ConfigVariable.h>
#include <Utils/ColorUtils.h>

namespace chams_vars
{



CONFIG_VARIABLE(Enabled, bool, false);

CONFIG_VARIABLE(EnemyColor, color::Rgba, (color::Rgba{255, 40, 40, 200}));

}
