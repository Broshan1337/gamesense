#pragma once

#include <Config/ConfigVariable.h>
#include "TriggerbotParams.h"

namespace triggerbot_vars
{

CONFIG_VARIABLE(Enabled, bool, false);
// Which bind activates the triggerbot while held (GameClient/Bind.h list; 0 = Off). Default MOUSE5.
CONFIG_VARIABLE_RANGE(HoldKey, triggerbot_params::kHoldKey);
// The low and high bounds of the reaction delay. Each target acquisition rolls a uniform delay
// between them; equal bounds = a fixed delay. See TriggerbotParams.h and Triggerbot::delaySeconds.
CONFIG_VARIABLE_RANGE(DelayMilliseconds, triggerbot_params::kDelayMilliseconds);
CONFIG_VARIABLE_RANGE(DelayMillisecondsMax, triggerbot_params::kDelayMillisecondsMax);

// Accuracy gate: only fire when the spread cone is small enough that the bullet lands on the target
// (see TriggerbotParams.h / Triggerbot::wouldShotLand). Off by default - the plain triggerbot fires
// regardless of the cone.
CONFIG_VARIABLE(AccuracyCheck, bool, false);
CONFIG_VARIABLE_RANGE(AccuracyRadius, triggerbot_params::kAccuracyRadius);

// Only fire when the crosshair is on the enemy's HEAD (a sphere at the head bone), not the body. Off by
// default. See Triggerbot::onHead.
CONFIG_VARIABLE(HeadOnly, bool, false);

// Hitchance gate: only fire when the predicted fraction of bullets landing on the target is at least
// this percentage (Monte-Carlo over the real spread cone). 0 = off. See Triggerbot::passesHitchance.
CONFIG_VARIABLE_RANGE(Hitchance, triggerbot_params::kHitchance);

// "Only fire at minimum inaccuracy" (velocity-cs2's is_max_accuracy). When on, hold fire until the
// weapon is at its lowest possible inaccuracy for the current stance: for rifles/SMGs/pistols that means
// slowed to <= 34% of the weapon's run speed (i.e. counter-strafed to a near stop), for scoped snipers
// fully still/ducked, and in air only at the jump apex. Off by default. See Triggerbot::passesMaxAccuracyGate.
CONFIG_VARIABLE(MaxAccuracyOnly, bool, false);

// Wall check: only fire when the target is actually visible - a world trace from the eye to the target's
// head/chest must reach it without a solid wall in between. Off by default. This is the first consumer of
// the new CS2 trace primitive (GameClient/Tracing/Tracing.h); see Triggerbot::passesVisibility.
CONFIG_VARIABLE(WallCheck, bool, false);

// Autowall: shoot THROUGH a world wall when it is thin enough that the bullet would penetrate. When on,
// an occluded target that would otherwise be held by the wall check is fired at if the wall between us is
// no thicker than AutowallMaxThickness. The game's own FireBullet performs the real penetration and
// damage - this only decides whether to pull the trigger. Off by default. See Triggerbot::passesVisibility.
CONFIG_VARIABLE(Autowall, bool, false);
CONFIG_VARIABLE_RANGE(AutowallMaxThickness, triggerbot_params::kAutowallMaxThickness);

// Silent spread + punch compensation for triggerbot shots: rewrites this command's input_history view
// samples with angles that cancel the weapon's predicted cone (and subtract aim punch), so the fired
// bullet lands ON the crosshair instead of wherever the scatter takes it - the difference between
// hitting and missing grows with movement, air time and spray length (i.e. per gun). Same correction
// engine as the rage bot (SubtickShotWriter), applied silently: your rendered view never moves.
// ON by default.
CONFIG_VARIABLE(SpreadCompensation, bool, true);

}
