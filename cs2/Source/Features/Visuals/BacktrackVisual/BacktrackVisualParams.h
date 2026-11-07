#pragma once

#include <cstdint>
#include <Config/RangeConstrainedVariableParams.h>

namespace backtrack_visual_params
{

// How to color the records: the age gradient (newest bright -> oldest dark)
// or one fixed user color (age still fades the alpha).
constexpr auto kColorMode = RangeConstrainedVariableParams<std::uint8_t>{.min = 0, .max = 1, .def = 0};

// Ghost linger (x88-style persistence): how long a silhouette stays on screen
// after its record expires out of the lag window (or the motion/wall gates
// hide it), fading out over the duration instead of popping. 0 = old behavior.
constexpr auto kLingerMs = RangeConstrainedVariableParams<std::uint16_t>{.min = 0, .max = 2000, .def = 600};

}
