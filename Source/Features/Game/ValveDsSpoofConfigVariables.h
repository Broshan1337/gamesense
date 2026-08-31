#pragma once

#include <Config/ConfigVariable.h>

// While on, forces the client's m_bIsValveDS byte to false every tick (see IsValveDsSpoof.h).
CONFIG_VARIABLE(ValveDsSpoofEnabled, bool, false);
