#pragma once

#include "Hitmarker/HitmarkerState.h"
#include "ModelGlow/ModelGlowState.h"
#include "PlayerInfoInWorld/PlayerInfoInWorldState.h"
#include "WorldColors/WorldColorsState.h"

struct VisualFeaturesStates {
    PlayerInfoInWorldState playerInfoInWorldState;
    ModelGlowState modelGlowState;
    HitmarkerState hitmarkerState;
    WorldColorsState worldColorsState;
};

