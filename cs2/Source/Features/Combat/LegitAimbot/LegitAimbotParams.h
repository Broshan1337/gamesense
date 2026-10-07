#pragma once

#include <cstdint>
#include <Config/RangeConstrainedVariableParams.h>
#include <GameClient/Bind.h>

namespace legit_aimbot_params
{



constexpr auto kFov = RangeConstrainedVariableParams<std::uint8_t>{.min = 0, .max = 180, .def = 5};



constexpr auto kSmooth = RangeConstrainedVariableParams<std::uint8_t>{.min = 1, .max = 20, .def = 5};




constexpr auto kAimKey = RangeConstrainedVariableParams<std::uint8_t>{.min = 0, .max = static_cast<std::uint8_t>(Bind::kLast), .def = Bind::kMouse5};

}
