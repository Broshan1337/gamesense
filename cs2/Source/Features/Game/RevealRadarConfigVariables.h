#pragma once

#include <Config/ConfigVariable.h>

namespace reveal_radar_vars
{

// Client-side radar reveal (skeet's "reveal radar"): write the spotted flag on every alive enemy
// pawn's client copy so the in-game radar draws them without the server's spotting rules. Off by
// default.
CONFIG_VARIABLE(Enabled, bool, false);

}
