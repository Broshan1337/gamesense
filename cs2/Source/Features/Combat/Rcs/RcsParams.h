#pragma once

#include <cstdint>
#include <Config/RangeConstrainedVariableParams.h>

namespace rcs_params
{







// Pitch (Y) and yaw (X) compensation are split: full pitch+yaw returns the
// crosshair to the pre-recoil aim point; a partial yaw moves it sideways off
// where the player put it (the friend report). Defaults are EXACT compensation
// (100) - the old 200 default over-compensated 2x.
constexpr auto kStrength = RangeConstrainedVariableParams<std::uint8_t>{.min = 0, .max = 255, .def = 100};
constexpr auto kYawStrength = RangeConstrainedVariableParams<std::uint8_t>{.min = 0, .max = 255, .def = 100};

}
