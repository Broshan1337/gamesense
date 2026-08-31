#pragma once

#include <Config/ConfigVariable.h>
#include <Config/RangeConstrainedVariableParams.h>

// Master switch for the FVA-style input_history rotation chains (see FvaEmulator.h).
//
// ON BY DEFAULT, deliberately. In the default self-echo mode the emitter is a no-op whenever it
// matters to be: idle ticks publish nothing (the wire is byte-identical to vanilla), the base
// message is never touched, and the structural validators disarm the feature instead of guessing.
// The switch stays so there is always a kill bolt, not because the feature needs hiding.
CONFIG_VARIABLE(FvaEnabled, bool, true);

// The decompiled original's literal reference-cache behaviour: chains always start at (0,0) even
// on idle ticks, which maximises the "nonsense delta" effect but is equally trivially detectable.
// Kept opt-in on purpose - this is the one mode whose idle-tick output is NOT vanilla-shaped.
CONFIG_VARIABLE(FvaZeroOriginSpoof, bool, false);

// Stamps a fresh engine-derived RNG seed onto the command after each published chain. Off by
// default: it diverges the seed field from what the engine itself wrote, which only makes sense
// for the armed-endpoint experiments.
CONFIG_VARIABLE(FvaReseedChains, bool, false);

// EXPERIMENT: publishes chains BEFORE the shot writer so SubtickShotWriter takes the per-entry
// silent redirect (shot claimed at the aim angle via input_history, base viewangles untouched).
// Only meaningful on a server whose shot resolution HONORS input_history - on servers that
// resolve from base viewangles (this build's default matchmaking behavior) the redirected shot
// misses. Off = the proven late-publish composition where the shot writer sees an empty field.
CONFIG_VARIABLE(FvaSilentShots, bool, false);

// Gate mirroring the decompiled original's firing-flag phase: publish chains only on ticks where
// the player is actually firing (primary attack held). The reference later dropped this gate in
// favour of always-running self-echo chains; kept here as an opt-in for that exact wire shape.
CONFIG_VARIABLE(FvaChainOnFireOnly, bool, false);

namespace fva_params
{
// Chain entries published per tick. The engine processes history sequentially within a tick, so
// more entries means finer-grained intermediate angles; beyond ~15 the extra resolution stops
// changing anything server-side while costing allocation pressure every turn. One entry is kept
// legal for "just clamp the endpoint" experiments.
constexpr auto kSubsteps = RangeConstrainedVariableParams<std::uint8_t>{.min = 1, .max = 15, .def = 8};
} // namespace fva_params

namespace fva_vars
{
CONFIG_VARIABLE_RANGE(Substeps, fva_params::kSubsteps);
} // namespace fva_vars
