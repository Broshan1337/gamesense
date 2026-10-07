#pragma once

#include <Config/ConfigVariable.h>
#include "RcsParams.h"

namespace rcs_vars
{





CONFIG_VARIABLE(Enabled, bool, false);
CONFIG_VARIABLE_RANGE(Strength, rcs_params::kStrength);

}
