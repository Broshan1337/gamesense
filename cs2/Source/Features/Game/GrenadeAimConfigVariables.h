#pragma once

#include <Config/ConfigVariable.h>

namespace grenade_aim_vars
{

// Saved-throw grenade aim (deadlocked's design, 2026-10-15): aims at the
// recorded angles of the closest saved throw with the same grenade on the
// current map while the pin is pulled.
CONFIG_VARIABLE(Enabled, bool, false);

}