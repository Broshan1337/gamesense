#pragma once

#include <Config/ConfigVariable.h>
#include "ViewmodelModParams.h"

namespace viewmodel_mod_vars
{

CONFIG_VARIABLE(ModifyFov, bool, true);
CONFIG_VARIABLE_RANGE(Fov, viewmodel_mod_params::kFov);

// Viewmodel position (the game's own viewmodel_offset_x/y/z cvars, forced while enabled and
// restored exactly on disable - the Sky Bloom pattern). Defaults are the game's stock offsets,
// so enabling alone changes nothing until a slider moves.
CONFIG_VARIABLE(ModifyPosition, bool, false);
CONFIG_VARIABLE_RANGE(OffsetX, viewmodel_mod_params::kOffsetX);
CONFIG_VARIABLE_RANGE(OffsetY, viewmodel_mod_params::kOffsetY);
CONFIG_VARIABLE_RANGE(OffsetZ, viewmodel_mod_params::kOffsetZ);

}
