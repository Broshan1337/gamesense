#pragma once

#include <Config/ConfigVariable.h>
#include <Config/RangeConstrainedVariableParams.h>

#include <cstdint>

namespace name_animator_params
{
// 0 Typewriter, 1 Glitch (L2R symbol decode), 2 Marquee (scroll), 3 Scramble (random-order
// decode), 4 Binary (0/1 decode), 5 Flicker (full text, random chars swap each frame),
// 6 Backwards (assembles from the end).
inline constexpr auto kMode = RangeConstrainedVariableParams<std::uint8_t>{.min = 0, .max = 6, .def = 0};
// Slider reads as SPEED (high = fast): internally inverted to ticks-per-frame
// (64 tps; 32 = ~30 updates/sec, 2 = ~2 updates/sec).
inline constexpr auto kSpeed = RangeConstrainedVariableParams<std::uint8_t>{.min = 2, .max = 32, .def = 8};
}

namespace name_animator_vars
{

// NAME ANIMATOR: animates the LIVE in-game name (the setinfo + USERINFO-flag rename path) through
// the text typed into the sidecar buffer. Every frame is a real mid-match rename - Steam persona
// untouched, no rate limit. The name reverts to the persona on reconnect; re-enabling restarts.
CONFIG_VARIABLE(Enabled, bool, false);
CONFIG_VARIABLE_RANGE(Mode, name_animator_params::kMode);
CONFIG_VARIABLE_RANGE(Speed, name_animator_params::kSpeed);

}
