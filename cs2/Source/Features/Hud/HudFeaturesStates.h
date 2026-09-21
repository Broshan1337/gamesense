#pragma once

#include "BombTimer/BombTimerState.h"
#include "BombPlantAlert/BombPlantAlertState.h"
#include "DefusingAlert/DefusingAlertState.h"
#include "PostRoundTimer/PostRoundTimerState.h"
#include "Watermark/WatermarkState.h"

struct HudFeaturesStates {
    BombTimerState bombTimerState;
    DefusingAlertState defusingAlertState;
    PostRoundTimerState postRoundTimerState;
    BombPlantAlertState bombPlantAlertState;
    WatermarkState watermarkState;
};
