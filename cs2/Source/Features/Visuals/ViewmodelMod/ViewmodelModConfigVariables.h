#pragma once

#include <Config/ConfigVariable.h>
#include "ViewmodelModParams.h"

namespace viewmodel_mod_vars
{

CONFIG_VARIABLE(ModifyFov, bool, true);
CONFIG_VARIABLE_RANGE(Fov, viewmodel_mod_params::kFov);




CONFIG_VARIABLE(ModifyPosition, bool, false);
CONFIG_VARIABLE_RANGE(OffsetX, viewmodel_mod_params::kOffsetX);
CONFIG_VARIABLE_RANGE(OffsetY, viewmodel_mod_params::kOffsetY);
CONFIG_VARIABLE_RANGE(OffsetZ, viewmodel_mod_params::kOffsetZ);

}
