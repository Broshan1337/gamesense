#pragma once

#include <cstdint>
#include <Config/RangeConstrainedVariableParams.h>

namespace rcs_params
{







// pitch (Y) and yaw (X) split: full pitch+yaw returns the crosshair to the
// pre-recoil aim point. defaults 100 = exact compensation: the punch getter
// passes the x2 scale flag (PlayerPawn::aimPunchAngle), so the value read is
// the same punch the server adds to the shot - 100 subtracts all of it.
// (history: the reader used to omit the scale flag and saw half the punch,
// which is why the old default 200 felt right and 100 felt like a no-op.)
constexpr auto kStrength = RangeConstrainedVariableParams<std::uint8_t>{.min = 0, .max = 255, .def = 100};
constexpr auto kYawStrength = RangeConstrainedVariableParams<std::uint8_t>{.min = 0, .max = 255, .def = 100};

}
