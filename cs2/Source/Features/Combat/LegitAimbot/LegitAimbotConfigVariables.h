#pragma once

#include <Config/ConfigVariable.h>
#include <Features/Combat/TargetSelection.h>
#include "LegitAimbotParams.h"

namespace legit_aimbot_vars
{

// The Legit-tab aim assist: while the aim key (MOUSE4) is held, smoothly steps the real view toward the
// nearest enemy hitbox within FOV. Distinct from the Rage-tab silent aimbot (which redirects the shot
// without moving the view) - this moves the actual camera, so it reads as if the player aimed.
CONFIG_VARIABLE(Enabled, bool, false);
CONFIG_VARIABLE_RANGE(TargetSelection, target_selection::kMode);
CONFIG_VARIABLE(TargetLock, bool, true);
CONFIG_VARIABLE(WallCheck, bool, true);
// Which bind activates the aim assist while held (GameClient/Bind.h list; 0 = Off). Default MOUSE5.
CONFIG_VARIABLE_RANGE(AimKey, legit_aimbot_params::kAimKey);
CONFIG_VARIABLE_RANGE(Fov, legit_aimbot_params::kFov);
CONFIG_VARIABLE_RANGE(Smooth, legit_aimbot_params::kSmooth);

// Draw the FOV circle on screen - a round panel centred on the crosshair marking the FOV boundary,
// drawn the same way as the no-scope inaccuracy visual, with a near-transparent fill (alpha ~20).
// This is velocity's legit visualize_fov (it used to hang off the rage aimbot; rage has no FOV visual
// in velocity - its max_fov is an invisible per-point gate).
CONFIG_VARIABLE(DrawFov, bool, false);
// Full RGBA circle color (picked with the color picker in the UI). The alpha channel styles the
// circle border; the fill keeps its fixed near-transparent alpha. Default matches the old green
// hue-140 look.
CONFIG_VARIABLE(FovCircleColor, color::Rgba, (color::Rgba{0, 255, 85, 255}));

// Acceptance region = the weapon's CURRENT spread circle instead of the Fov slider above: the assist
// only engages on targets whose crosshair angle fits inside GetInaccuracy+GetSpread right now - it
// disengages automatically while the cone is wide (moving/jumping/spraying) and tightens up when
// standing still. Off by default; when off, the Fov slider applies.
CONFIG_VARIABLE(SpreadCircleFov, bool, false);

// Which body parts are eligible, tried Head > Chest > Stomach > Arms > Legs. Default head + chest, the
// usual legit targets. NOTE: only the head bone index is confirmed - see AimTarget.h.
CONFIG_VARIABLE(HitHead, bool, true);
CONFIG_VARIABLE(HitChest, bool, true);
CONFIG_VARIABLE(HitStomach, bool, false);
CONFIG_VARIABLE(HitArms, bool, false);
CONFIG_VARIABLE(HitLegs, bool, false);

}
