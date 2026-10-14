#pragma once

#include <Config/ConfigVariable.h>
#include "RcsParams.h"

namespace rcs_vars
{

CONFIG_VARIABLE(Enabled, bool, false);
CONFIG_VARIABLE_RANGE(Strength, rcs_params::kStrength);
// Yaw (horizontal) compensation strength, separate from pitch - full X+Y keeps
// the crosshair where the player put it, partial yaw drifts it sideways.
CONFIG_VARIABLE_RANGE(YawStrength, rcs_params::kYawStrength);

}