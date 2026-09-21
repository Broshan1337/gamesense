#pragma once

#include <Config/ConfigVariable.h>
#include <Config/RangeConstrainedVariableParams.h>

#include <cstdint>

namespace name_animator_params
{
// 0 Typewriter, 1 Glitch (L2R symbol decode), 2 Marquee (scroll), 3 Scramble (random-order
// decode), 4 Binary (0/1 decode), 5 Flicker (full text, random chars swap each frame),
// 6 Backwards (typed in REVERSE - "olleh" grows from the name's end),
// 7 Spongebob (mocking aLtErNaTiNg cAsE), 8 Pulse (caps segment teleports by quarters),
// 9 Strobe (hard cuts SHOUT/whisper/spaced), 10 Wave (case sine sweeps), 11 Crawler
// (gradient scanner bar), 12 Storm (heavy churn),
// 13 Nystagmus (whole name jitters left/right 1-2 spaces per frame), 14 Emoji Strobe
// (bright emoji prefix slamming through a cycle), 15 Flashbang (alternates name <-> solid
// block bar), 16 Twitch (noise swap + random left/right shifts), 17 Face (kaomoji chaos
// storm - faces spawn around the name every frame),
// 18 Super Wave (superscript small-glyph wave), 19 RLO Flip (U+202E mirrored alternation),
// 20 Vaporwave (fullwidth morph wave), 21 Invisible Chaos (zero-width injection - string
// differs every rename, looks identical), 22 Zalgo (NastyText-recipe combining-mark stacks).
inline constexpr auto kMode = RangeConstrainedVariableParams<std::uint8_t>{.min = 0, .max = 22, .def = 0};
// Slider reads as SPEED (high = fast): internally inverted to ticks-per-frame
// (64 tps; Speed 64 = every tick ~64 renames/sec - the engine's visible ceiling, the server
// coalesces anything faster; Speed 2 = ~1/sec). NOTE: ticksPerFrame = kSpeedMax - Speed + 1, the slider is scale-independent.
inline constexpr auto kSpeed = RangeConstrainedVariableParams<std::uint8_t>{.min = 2, .max = 64, .def = 8};
}

namespace name_animator_vars
{

// NAME ANIMATOR: animates the LIVE in-game name (the setinfo + USERINFO-flag rename path) through
// the text typed into the sidecar buffer. Every frame is a real mid-match rename - Steam persona
// untouched, no rate limit. The name reverts to the persona on reconnect; re-enabling restarts.
CONFIG_VARIABLE(Enabled, bool, false);
CONFIG_VARIABLE_RANGE(Mode, name_animator_params::kMode);
CONFIG_VARIABLE_RANGE(Speed, name_animator_params::kSpeed);
// Direct net-message renames: each animation frame goes out as a genuine CNETMsg_SetConVar
// (record 6) through the game's own net channel (GameClient/NetMessageFactory.h pipeline) instead
// of the `setinfo` console path - the engine's userinfo-change machinery coalesces console-driven
// renames (the "frozen modes" saga), the direct path makes every frame land at full rate. The
// local `name` cvar is left untouched in this mode: the scoreboard name comes from the server's
// userinfo row either way. Config key appended LAST in the NameAnimator object (ordering rule).
CONFIG_VARIABLE(DirectSend, bool, false);

}
