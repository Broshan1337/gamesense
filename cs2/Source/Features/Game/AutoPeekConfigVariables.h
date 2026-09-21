#pragma once

#include <Config/ConfigVariable.h>

namespace autopeek_vars
{

// skeet's auto peek ("quick peek"): while enabled, the standstill position is ANCHORED and, once
// the player peeks out, the feature counter-drives them back to the anchor every tick - through
// the movement buttons AND an explicit subtick analog step - until they are standing on it again.
// Off by default.
CONFIG_VARIABLE(Enabled, bool, false);

}
