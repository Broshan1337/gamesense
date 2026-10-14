#pragma once

#include <Config/ConfigVariable.h>
#include "TriggerbotParams.h"

namespace triggerbot_vars
{

CONFIG_VARIABLE(Enabled, bool, false);

CONFIG_VARIABLE_RANGE(HoldKey, triggerbot_params::kHoldKey);

// Activation mode for HoldKey: 0 = Hold, 1 = Toggle (press flips the latch).
CONFIG_VARIABLE_RANGE(KeyMode, triggerbot_params::kKeyMode);


CONFIG_VARIABLE_RANGE(DelayMilliseconds, triggerbot_params::kDelayMilliseconds);
CONFIG_VARIABLE_RANGE(DelayMillisecondsMax, triggerbot_params::kDelayMillisecondsMax);




CONFIG_VARIABLE(AccuracyCheck, bool, false);
CONFIG_VARIABLE_RANGE(AccuracyRadius, triggerbot_params::kAccuracyRadius);



// Bone selection (friend request): which hitgroups may trigger the shot.
// All three default ON = the old any-hitbox behavior; only head = old HeadOnly.
CONFIG_VARIABLE(HitHead, bool, true);
CONFIG_VARIABLE(HitChest, bool, true);
CONFIG_VARIABLE(HitStomach, bool, true);

// Rapid refire (friend request "automatic pistols"): after the first gated
// shot at a target, subsequent shots at the SAME target skip the accuracy/
// reaction gates and click as fast as the weapon fires - semi-auto pistols
// keep hopping while the crosshair is held on.
CONFIG_VARIABLE(AutoRefire, bool, false);

CONFIG_VARIABLE_RANGE(AimTimeMs, triggerbot_params::kAimTimeMs);

// Pause between shots at the same target while Rapid Refire clicks
// (friend request: "delay for pistols like Dual Berettas").
CONFIG_VARIABLE_RANGE(RefireDelayMs, triggerbot_params::kRefireDelayMs);

// Delay humanizer (friend request): multiplies every rolled delay (reaction +
// refire) by a random 0.75..1.25, so the clicks don't land on a metronome.
CONFIG_VARIABLE(HumanizeDelays, bool, true);



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
