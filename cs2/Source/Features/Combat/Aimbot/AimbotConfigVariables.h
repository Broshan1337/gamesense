#pragma once

#include <Config/ConfigVariable.h>
#include <Features/Combat/TargetSelection.h>
#include "AimbotParams.h"

namespace aimbot_vars
{

CONFIG_VARIABLE(Enabled, bool, false);
CONFIG_VARIABLE_RANGE(TargetSelection, target_selection::kMode);
CONFIG_VARIABLE(TargetLock, bool, true);









CONFIG_VARIABLE(HitHead, bool, true);
CONFIG_VARIABLE(HitChest, bool, false);
CONFIG_VARIABLE(HitStomach, bool, false);
CONFIG_VARIABLE(HitArms, bool, false);
CONFIG_VARIABLE(HitLegs, bool, false);






CONFIG_VARIABLE(SpreadCompensation, bool, false);






CONFIG_VARIABLE(RecoilCompensation, bool, true);








CONFIG_VARIABLE(ForceShot, bool, false);
CONFIG_VARIABLE(ForceShotAir, bool, false);




CONFIG_VARIABLE(Extrapolate, bool, false);
CONFIG_VARIABLE_RANGE(ExtrapolateTicks, aimbot_params::kExtrapolateTicks);



CONFIG_VARIABLE(BodyAim, bool, false);


CONFIG_VARIABLE_RANGE(Hitchance, aimbot_params::kHitchance);


CONFIG_VARIABLE_RANGE(MinDamage, aimbot_params::kMinDamage);



CONFIG_VARIABLE(Multipoint, bool, false);




CONFIG_VARIABLE_RANGE(PointScale, aimbot_params::kPointScale);
CONFIG_VARIABLE(DynamicPointscale, bool, true);






CONFIG_VARIABLE(WallCheck, bool, true);
CONFIG_VARIABLE(Autowall, bool, false);




CONFIG_VARIABLE(Backtrack, bool, false);
CONFIG_VARIABLE_RANGE(BacktrackTicks, aimbot_params::kBacktrackTicks);





CONFIG_VARIABLE(SpreadCircleFov, bool, false);








CONFIG_VARIABLE(AutoStop, bool, false);










CONFIG_VARIABLE(SpreadGate, bool, false);







CONFIG_VARIABLE(SeedFallback, bool, false);






CONFIG_VARIABLE(ForceShotWait, bool, false);
CONFIG_VARIABLE_RANGE(ForceShotWaitTicks, aimbot_params::kForceShotWaitTicks);




}
