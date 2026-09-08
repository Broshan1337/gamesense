#pragma once

#include <cstdint>

#include <Config/ConfigVariable.h>

// Local agent model changer (the agent counterpart to the knife impersonation): picks an
// agent def; while in-game the local pawn's model is swapped to that agent via the game's own
// SetModel, and the selection persists in the config like every other changer setting.
// Purely local/cosmetic - the server keeps its own idea of the loadout.
namespace agent_changer_vars
{

// The agent's def index (0 = default model). Pickable from ItemDefDatabase's kAgentItems.
CONFIG_VARIABLE(AgentDef, std::uint16_t, 0);

}
