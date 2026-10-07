#pragma once

#include <Config/ConfigVariable.h>
#include <Features/Combat/TargetSelection.h>
#include "LegitAimbotParams.h"

namespace legit_aimbot_vars
{




CONFIG_VARIABLE(Enabled, bool, false);
CONFIG_VARIABLE_RANGE(TargetSelection, target_selection::kMode);
CONFIG_VARIABLE(TargetLock, bool, true);
CONFIG_VARIABLE(WallCheck, bool, true);

CONFIG_VARIABLE_RANGE(AimKey, legit_aimbot_params::kAimKey);
CONFIG_VARIABLE_RANGE(Fov, legit_aimbot_params::kFov);
CONFIG_VARIABLE_RANGE(Smooth, legit_aimbot_params::kSmooth);





CONFIG_VARIABLE(DrawFov, bool, false);



CONFIG_VARIABLE(FovCircleColor, color::Rgba, (color::Rgba{0, 255, 85, 255}));





CONFIG_VARIABLE(SpreadCircleFov, bool, false);



CONFIG_VARIABLE(HitHead, bool, true);
CONFIG_VARIABLE(HitChest, bool, true);
CONFIG_VARIABLE(HitStomach, bool, false);
CONFIG_VARIABLE(HitArms, bool, false);
CONFIG_VARIABLE(HitLegs, bool, false);

}
