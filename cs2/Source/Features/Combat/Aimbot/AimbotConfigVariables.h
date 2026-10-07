#pragma once

#include <Config/ConfigVariable.h>
#include <Features/Combat/TargetSelection.h>
#include "AimbotParams.h"

namespace aimbot_vars
{

CONFIG_VARIABLE(Enabled, bool, false);
CONFIG_VARIABLE_RANGE(TargetSelection, target_selection::kMode);
CONFIG_VARIABLE(TargetLock, bool, true);

// NOTE: no user FOV. The rage aimbot gates targets with velocity's fixed per-point max_fov (180 deg,
// see aimbot_params::kMaxFov) - matching velocity exactly, where max_fov is a non-UI group setting.
// The old FOV circle moved to the legit aimbot (velocity's visualize_fov lives there too).

// Which body parts are eligible targets. The aimbot aims at the highest-priority ENABLED part it can
// resolve on the chosen enemy, in this order: Head > Chest > Stomach > Arms > Legs. Default is
// head-only. NOTE: only the head bone index (6) is confirmed; the others are best-guess CS2 player
// skeleton indices to be verified/tuned in-game (Aimbot::targetBonePosition).
CONFIG_VARIABLE(HitHead, bool, true);
CONFIG_VARIABLE(HitChest, bool, false);
CONFIG_VARIABLE(HitStomach, bool, false);
CONFIG_VARIABLE(HitArms, bool, false);
CONFIG_VARIABLE(HitLegs, bool, false);

// Cancel the shot's predicted bullet spread so the silently-redirected shot lands on the target even
// while moving. When on, the aimbot writes an aim angle (including roll) computed from the game's own
// spread functions (SpreadSolver) that steers the deflected bullet back onto the aimed hitbox; when
// off, it writes the raw hitbox angle (the previous behavior). On by default - toggle it off to A/B
// test that compensation is actually helping (fire moving with it off vs on).
CONFIG_VARIABLE(SpreadCompensation, bool, true);

// Cancel recoil (aim punch) as well: CS2 fires along (view_angles + aim_punch), so this subtracts the
// current aim punch from the angle written into input_history, keeping a spray on target as the view
// kicks up - the same step velocity-cs2 does. Independent of SpreadCompensation (spread = the random
// cone, this = the deterministic recoil kick). Aim punch is ~0 before the first shot of a burst, so
// this only affects an ongoing spray, never a fresh/standing shot.
CONFIG_VARIABLE(RecoilCompensation, bool, true);

// Force shot / auto-shoot (velocity-cs2's force_shot / force_shot_air). When on, the rage aimbot fires
// on its own - no held mouse button - the instant it has a target in FOV and the weapon is at its
// minimum inaccuracy (velocity: force = force_shot && is_max_accuracy). ForceShot gates the on-ground
// case, ForceShotAir the in-air case (fire only at the jump apex). The shot is taken by splicing an
// attack into the outgoing usercmd on the subtick path (AttackCommand: both banks + buttons_pb,
// attack1 history index), the same path the triggerbot uses; the aimbot's
// own silent redirect + spread compensation still steers it onto the target. Both off by default.
CONFIG_VARIABLE(ForceShot, bool, false);
CONFIG_VARIABLE(ForceShotAir, bool, false);

// Target extrapolation: lead a moving enemy by predicting where they will be (velocity + gravity + wall
// traces) instead of aiming where they currently are. Extrapolate leads by ExtrapolateTicks ticks. Off by
// default. See TargetExtrapolator and AimTarget::best.
CONFIG_VARIABLE(Extrapolate, bool, false);
CONFIG_VARIABLE_RANGE(ExtrapolateTicks, aimbot_params::kExtrapolateTicks);

// Force the aim onto the body (chest, falling back to stomach) regardless of the hitbox toggles above -
// velocity's "force b-aim". A more reliable auto-shoot target than the head while moving. Off by default.
CONFIG_VARIABLE(BodyAim, bool, false);

// Rage auto-shoot hitchance gate (percent). See aimbot_params::kHitchance. 0 = off.
CONFIG_VARIABLE_RANGE(Hitchance, aimbot_params::kHitchance);

// Rage auto-shoot min-damage gate. See aimbot_params::kMinDamage. 0 = off.
CONFIG_VARIABLE_RANGE(MinDamage, aimbot_params::kMinDamage);

// Multipoint (velocity's dynamic_pointscale): aim at the best-hitchance point across the REAL hitbox
// capsule rather than only the bone centre. Off by default. See Aimbot::refineMultipoint / MultiPoint.
CONFIG_VARIABLE(Multipoint, bool, false);

// Point scale percent (velocity's `pointscale`, default 85): how far off-centre multipoint candidates
// may sit on the hitbox capsule. DynamicPointscale (velocity's dynamic_pointscale, default on) then
// pulls each candidate toward the centre by the probability the CURRENT inaccuracy cone reaches it.
CONFIG_VARIABLE_RANGE(PointScale, aimbot_params::kPointScale);
CONFIG_VARIABLE(DynamicPointscale, bool, true);

// Rage visibility gates (velocity's rage target selection). WallCheck: only accept targets a world
// trace can actually reach (clear line of sight to the aimed point; hitting the target itself counts).
// Autowall: relax that to walls - the shot is accepted when Autowall::penetratedDamage says the
// bullet survives with at least MinDamage left (0 = any surviving damage). Off by default: without
// either gate every FOV target is considered, exactly like before these existed.
CONFIG_VARIABLE(WallCheck, bool, false);
CONFIG_VARIABLE(Autowall, bool, false);

// Backtracking (velocity's lagcomp): aim the silent shot at a recent PAST position of the target and
// stamp that tick into input_history so the server rewinds there. Off by default. BacktrackTicks bounds
// how far back. See Backtrack.h / Aimbot. (No visible effect on a local no-latency server.)
CONFIG_VARIABLE(Backtrack, bool, false);
CONFIG_VARIABLE_RANGE(BacktrackTicks, aimbot_params::kBacktrackTicks);

// Acceptance region = the weapon's CURRENT spread circle instead of the fixed 180-degree max_fov: a
// target is only considered when the crosshair-to-target angle fits inside GetInaccuracy+GetSpread
// right now, so the ragebot naturally holds fire while the cone is wide (moving/jumping/spraying).
// Off by default; when off, velocity's fixed max_fov gate applies.
CONFIG_VARIABLE(SpreadCircleFov, bool, false);

// velocity's autostop: while firing at a chosen target, press the counter-strafe buttons so the
// player decelerates to a stop - collapsing the weapon's inaccuracy to its standing-still cone, the
// state where spread compensation is exact. This is THE fix for "misses and lucky headshots while
// moving": the seed-based correction can only cancel the shot it correctly predicts, and while
// running, the deflection is so large that self-consistent corrections frequently do not exist.
// Buttons are only OR-ed into the current tick's command (rebuilt from real key state every tick),
// so releasing fire instantly hands movement back. Off by default.
CONFIG_VARIABLE(AutoStop, bool, false);

// Hold fire until the spread correction is exact (the friend-code CNoSpread's "skip attacking when
// no seed was found"). With SpreadCompensation on, the seed-based correction cancels the shot's
// predicted cone only when a self-consistent correction exists for the CURRENT angles/tick - while
// running or mid-spray the cone grows past what a consistent correction can reach, and the shot
// would leave on the raw angle (cone luck = the "missing while moving" symptom). With this gate on,
// such a tick fires NOTHING instead: the attack is stripped from the outgoing command (banks,
// subtick press step, attack marker) and the next tick - usually a narrower cone, helped by
// AutoStop - gets another chance. The result is fewer but near-pixel-perfect shots: velocity's
// "almost as good as a no-spread server" behavior. Off by default (raw shots leave as before).
CONFIG_VARIABLE(SpreadGate, bool, false);

// Seed-mode arm FOR the spread gate (needs SpreadGate + SpreadCompensation on to matter): when
// the solver cannot produce an exact correction for this tick (wide cone), the gate normally
// holds fire. With this on, a held tick still fires when the shot's PREDICTED seed - known in
// advance, derived from the angles we are about to write - happens to deflect the bullet onto
// the aimed hitbox anyway. The rage bot then takes exact-corrected shots, lucky-seed shots, and
// nothing else. Off by default.
CONFIG_VARIABLE(SeedFallback, bool, false);

// skeet's wait-for-accuracy fire gate for force-shot: when the stance/min-damage arms pass but
// the accuracy/hitchance arm does not, the auto-shot WAITS instead of giving up for the tick -
// it keeps re-evaluating each tick and fires the moment the measured accuracy clears, giving up
// only after ForceShotWaitTicks (skeet projects up to 34 ticks ahead; we wait on the real
// weapon state instead of a simulated recovery curve). Off by default.
CONFIG_VARIABLE(ForceShotWait, bool, false);
CONFIG_VARIABLE_RANGE(ForceShotWaitTicks, aimbot_params::kForceShotWaitTicks);


// NOTE: still to come - the Silent-vs-visible toggle. Its config var is added when built.

}
