#pragma once

#include <Config/ConfigVariable.h>
#include <Utils/ColorUtils.h>

namespace grenade_pred_vars
{
CONFIG_VARIABLE(Enabled, bool, false);
CONFIG_VARIABLE(Trajectory, bool, true);
CONFIG_VARIABLE(FlashTimer, bool, true);
CONFIG_VARIABLE(InfernoBounds, bool, true);
CONFIG_VARIABLE(TrajectoryColor, color::Rgba, (color::Rgba{130, 200, 255, 220}));
CONFIG_VARIABLE(FlashTimerColor, color::Rgba, (color::Rgba{255, 240, 130, 230}));
CONFIG_VARIABLE(InfernoColor, color::Rgba, (color::Rgba{255, 120, 60, 90}));
}
