#pragma once

#include <Config/ConfigVariable.h>
#include <Config/RangeConstrainedVariableParams.h>

#include <cstdint>

namespace movement_params
{
// NOTE: uint8, not int - the config schema's range branch supports HueInteger/uint8/float only.
inline constexpr auto kSlowWalkSpeed = RangeConstrainedVariableParams<std::uint8_t>{.min = 1, .max = 100, .def = 33};
}

// The movement suite (velocity-cs2's movement set minus edgebug): edgejump, edgestop, slowwalk,
// fastladder, jumpbug. All staged decisions are made at CreateMove and written through the same
// proven paths the bunnyhop uses (analog components at BuildUserCmd, buttons + view angles at
// WriteMoveCrc). See Movement.h.
namespace movement_vars
{

// Jump exactly at the last grounded tick before an edge: two hull sweeps ahead of the predicted
// position detect the edge (with a stair-step scan so stairs do not false-trigger), and the jump
// is pressed the tick before the floor disappears - preserving run speed over the drop.
CONFIG_VARIABLE(EdgeJump, bool, false);

// The opposite: detect that the player is at (or decelerating toward) an edge and counter-strafe
// / push the input back so they stop before falling. Suppressed while EdgeJump is on (the
// reference treats them as mutually exclusive).
CONFIG_VARIABLE(EdgeStop, bool, false);

// Scale the player's analog movement down while held-walking on the ground, so the wish speed
// stays under the audible/visible run threshold: SlowWalkSpeed percent of full movement.
CONFIG_VARIABLE(SlowWalk, bool, false);
CONFIG_VARIABLE_RANGE(SlowWalkSpeed, movement_params::kSlowWalkSpeed);

// Ladder climb re-aim: on a ladder, rewrite the analog components, move buttons and view angles
// to the ladder-optimal combination, climbing considerably faster than the default cap.
CONFIG_VARIABLE(FastLadder, bool, false);

// While falling with jump held, predict the touchdown fraction within THIS tick and write a
// duck-press/duck-release + jump-release/jump-pulse bracket at exactly that sub-tick moment, so
// the landing impulse is skipped (no fall damage, no speed loss) and the jump fires off the
// invisible contact.
CONFIG_VARIABLE(JumpBug, bool, false);

}
