#pragma once

#include "BombTimer/BombTimerState.h"
#include "BombPlantAlert/BombPlantAlertState.h"
#include "CombatStats/CombatStatsState.h"
#include "DefusingAlert/DefusingAlertState.h"
#include "PostRoundTimer/PostRoundTimerState.h"
#include "SpectatorList/SpectatorListState.h"
#include "StatusPanel/StatusPanelState.h"
#include "Watermark/WatermarkState.h"

struct HudFeaturesStates {
    BombTimerState bombTimerState;
    DefusingAlertState defusingAlertState;
    PostRoundTimerState postRoundTimerState;
    BombPlantAlertState bombPlantAlertState;
    WatermarkState watermarkState;
    StatusPanelState statusPanelState;
    CombatStatsState combatStatsState;
    SpectatorListState spectatorListState;
};
