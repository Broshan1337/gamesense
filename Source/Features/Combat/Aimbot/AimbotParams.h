#pragma once

#include <cstdint>
#include <Config/RangeConstrainedVariableParams.h>

namespace aimbot_params
{

// velocity-cs2's rage max_fov, applied THEIR way: not a user slider but a fixed config default of 180
// degrees used as a PER-POINT gate - every candidate aim point whose angle_distance(view, aim) exceeds
// this is skipped during the scan (rage.cpp:1111-1115). At 180 degrees effectively nothing is excluded
// by FOV; target selection is driven by the hitchance/damage scoring instead. We deliberately do NOT
// expose it as a slider (user decision: match velocity, no self-set FOV).
constexpr float kMaxFov = 180.0f;

// How many ticks ahead to lead a moving target when extrapolation is on (see TargetExtrapolator). 0
// leads not at all; a handful of ticks is plenty for CS2's 64-tick movement. Capped low because each
// tick runs a few hull traces per target.
constexpr auto kExtrapolateTicks = RangeConstrainedVariableParams<std::uint8_t>{.min = 0, .max = 16, .def = 2};

// Rage hitchance gate, as a percentage: the auto-shoot (force-shot) fires only when the predicted
// fraction of the weapon's spread cone that lands on the aimed point is at least this (Monte-Carlo over
// the game's own cone, like the triggerbot). 0 = off (force-shot then gates on max-accuracy only). This
// is velocity's rage `accurate = hitchance >= needed` path.
constexpr auto kHitchance = RangeConstrainedVariableParams<std::uint8_t>{.min = 0, .max = 100, .def = 0};

// Rage min-damage: the auto-shoot only fires if the shot to the aimed point would do at least this much
// damage (base falloff + velocity's armor/hitgroup scaling). 0 = off. Capped at 200 (well above a 100hp
// one-tap so "must one-tap" configs like 101 fit).
constexpr auto kMinDamage = RangeConstrainedVariableParams<std::uint8_t>{.min = 0, .max = 200, .def = 0};

// How many ticks back the backtracking may aim (velocity's max_backtrack_ticks). Bounded by the record
// ring (16). Only meaningful against real players with latency; ~no effect on a local server.
constexpr auto kBacktrackTicks = RangeConstrainedVariableParams<std::uint8_t>{.min = 1, .max = 16, .def = 12};

// skeet's wait-for-accuracy fire gate cap: how long the force-shot may keep waiting for the weapon's
// measured accuracy to clear before giving up on the engagement (skeet projects up to 34 ticks ahead).
constexpr auto kForceShotWaitTicks = RangeConstrainedVariableParams<std::uint8_t>{.min = 1, .max = 34, .def = 34};

// Multipoint point scale percent (velocity's `pointscale`, default 85): candidates may sit up to this
// fraction of the hitbox capsule radius off its centre.
constexpr auto kPointScale = RangeConstrainedVariableParams<std::uint8_t>{.min = 0, .max = 100, .def = 85};

}
