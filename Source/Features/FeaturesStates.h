#pragma once

#include "Combat/Aimbot/AimbotFovCircleState.h"
#include "Combat/SniperRifles/NoScopeInaccuracyVis/NoScopeInaccuracyVisState.h"
#include "Combat/SpreadCircleVis/SpreadCircleVisState.h"
#include "Hud/HudFeaturesStates.h"
#include "Visuals/VisualFeaturesStates.h"

struct FeaturesStates {
    HudFeaturesStates hudFeaturesStates;
    VisualFeaturesStates visualFeaturesStates;
    NoScopeInaccuracyVisState noScopeInaccuracyVisState;
    AimbotFovCircleState aimbotFovCircleState;
    SpreadCircleVisState spreadCircleVisState;
};
