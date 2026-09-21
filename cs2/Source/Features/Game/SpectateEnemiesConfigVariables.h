#pragma once

#include <Config/ConfigVariable.h>

namespace spectate_vars
{

// Spectate enemies while dead: while the toggle is on, the game's own spec_next/spec_prev keys
// cycle alive enemy pawns (the camera rides CPlayer_ObserverServices::m_hObserverTarget, which
// nothing re-validates per frame). Off by default.
CONFIG_VARIABLE(Enabled, bool, false);

}