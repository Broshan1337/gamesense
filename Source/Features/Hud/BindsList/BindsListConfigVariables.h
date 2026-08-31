#pragma once

#include <Config/ConfigVariable.h>

namespace binds_list_vars
{
// The in-game keybind list overlay (player-list style HUD panel listing every bind with its
// key; the key pill glows while the key is physically held). Off by default.
CONFIG_VARIABLE(Enabled, bool, false);
}
