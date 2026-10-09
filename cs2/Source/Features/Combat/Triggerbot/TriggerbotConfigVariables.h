#pragma once

#include <Config/ConfigVariable.h>
#include "TriggerbotParams.h"

namespace triggerbot_vars
{

CONFIG_VARIABLE(Enabled, bool, false);

CONFIG_VARIABLE_RANGE(HoldKey, triggerbot_params::kHoldKey);


CONFIG_VARIABLE_RANGE(DelayMilliseconds, triggerbot_params::kDelayMilliseconds);
CONFIG_VARIABLE_RANGE(DelayMillisecondsMax, triggerbot_params::kDelayMillisecondsMax);




CONFIG_VARIABLE(AccuracyCheck, bool, false);
CONFIG_VARIABLE_RANGE(AccuracyRadius, triggerbot_params::kAccuracyRadius);



CONFIG_VARIABLE(HeadOnly, bool, false);



CONFIG_VARIABLE_RANGE(Hitchance, triggerbot_params::kHitchance);





CONFIG_VARIABLE(MaxAccuracyOnly, bool, false);




CONFIG_VARIABLE(WallCheck, bool, false);





CONFIG_VARIABLE(Autowall, bool, false);
CONFIG_VARIABLE_RANGE(AutowallMaxThickness, triggerbot_params::kAutowallMaxThickness);







CONFIG_VARIABLE(SpreadCompensation, bool, true);









CONFIG_VARIABLE(SeededFire, bool, false);
CONFIG_VARIABLE(ThroughWalls, bool, false);
CONFIG_VARIABLE_RANGE(MinDamage, triggerbot_params::kMinDamage);
CONFIG_VARIABLE(Backtrack, bool, false);
CONFIG_VARIABLE_RANGE(BacktrackTicks, triggerbot_params::kBacktrackTicks);

}
