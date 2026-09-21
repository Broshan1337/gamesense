#pragma once

#include <cstdint>
#include <Config/RangeConstrainedVariableParams.h>
#include <GameClient/Bind.h>

namespace triggerbot_params
{

// Reaction delay in milliseconds, from the crosshair landing on an enemy to the shot going out.
//
// The delay is a RANGE, not a single number: two sliders give a low bound and a high bound, and each
// time the crosshair acquires a fresh target the triggerbot rolls a new delay uniformly between them
// (see Triggerbot::delaySeconds). Set the two equal for a fixed delay; spread them for a human-looking
// reaction time that is never the exact same number twice. kDelayMilliseconds is the LOW bound,
// kDelayMillisecondsMax the HIGH bound; if the user drags them past each other the code swaps them, so
// either ordering is safe.
//
// Kept in milliseconds because that is the unit the number is thought about in, but be aware of the
// floor underneath it: commands are built once per tick, so on a 64-tick server nothing finer than
// ~15.6ms is actually representable and small values all collapse onto "the very next command".
// The range starts at 0 because that IS the useful setting for testing - it fires as fast as the
// input path allows, which is the clearest way to see the feature working at all.
//
// The 255 ceiling is std::uint8_t's, inherited from the shared slider path (IntSlider and
// SetCommandHandler are uint8_t end to end). That is a real constraint rather than a chosen number,
// but it costs nothing here: typical human reaction time is around 200-250ms, so the whole range a
// reaction delay could sensibly want already fits underneath it. Widening it would mean widening
// the slider plumbing that two shipped features already depend on.
//
// Defaults sit at 50-120ms: a plausible human reaction spread, and (because triggerbot config is not
// persisted - it is in-memory only, reset every session) this is what the feature does out of the box.
constexpr auto kDelayMilliseconds = RangeConstrainedVariableParams<std::uint8_t>{.min = 0, .max = 255, .def = 50};
constexpr auto kDelayMillisecondsMax = RangeConstrainedVariableParams<std::uint8_t>{.min = 0, .max = 255, .def = 120};

// Accuracy gate: the largest bullet deviation, in world units, we will still take a shot at. The
// current spread cone (GetInaccuracy+GetSpread) times the distance to the target is the worst-case
// lateral miss; if that exceeds this radius we hold fire, so while moving the triggerbot only shoots
// when the cone has settled enough for the bullet to actually land. Default ~16u = a body half-width.
// Tunable because the exact scale of the cone value is confirmed in-game, not assumed here.
constexpr auto kAccuracyRadius = RangeConstrainedVariableParams<std::uint8_t>{.min = 1, .max = 64, .def = 16};

// Hitchance gate, as a percentage. The triggerbot Monte-Carlo-samples the weapon's real spread cone
// (the game's own seed+cone functions, via SpreadSolver) and estimates what fraction of bullets would
// land on the target; it only fires when that fraction is at least this. 0 = off (the geometric
// AccuracyCheck above is the simpler alternative). Higher = more conservative. Default 0 (off) so the
// feature is opt-in, like AccuracyCheck.
constexpr auto kHitchance = RangeConstrainedVariableParams<std::uint8_t>{.min = 0, .max = 100, .def = 0};

// Autowall: the thickest world wall (in world units) the triggerbot will still shoot THROUGH when the
// target is occluded. Measured by tracing to the wall's near face and back from the target to its far
// face; if the gap is within this, the shot is taken (the game's own FireBullet does the real
// penetration + damage - we only decide whether to fire). Default 8u ~ a thin wall/door.
constexpr auto kAutowallMaxThickness = RangeConstrainedVariableParams<std::uint8_t>{.min = 1, .max = 40, .def = 8};

// Hold key: which bind activates the triggerbot while held (GameClient/Bind.h encoding:
// 0 = Off, 1..248 = scancode, 249+ = mouse). Captured CS2-settings style in the UI. Default
// MOUSE5, what this feature was hardcoded to before binds became configurable.
constexpr auto kHoldKey = RangeConstrainedVariableParams<std::uint8_t>{.min = 0, .max = static_cast<std::uint8_t>(Bind::kLast), .def = Bind::kMouse5};

}
