#pragma once

#include <Config/ConfigVariable.h>
#include "RcsParams.h"

namespace rcs_vars
{

// Standalone recoil control (a Legit-tab feature): during a spray, moves the real view against the
// recoil kick so bullets keep landing where the crosshair points. Automatic while firing (no key) -
// distinct from the Rage aimbot's silent recoil compensation, which corrects the shot without moving
// the view. Off by default.
CONFIG_VARIABLE(Enabled, bool, false);
CONFIG_VARIABLE_RANGE(Strength, rcs_params::kStrength);

}
