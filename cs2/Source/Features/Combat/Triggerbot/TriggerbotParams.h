#pragma once

#include <cstdint>
#include <Config/RangeConstrainedVariableParams.h>
#include <GameClient/Bind.h>

namespace triggerbot_params
{
























constexpr auto kDelayMilliseconds = RangeConstrainedVariableParams<std::uint8_t>{.min = 0, .max = 255, .def = 50};
constexpr auto kDelayMillisecondsMax = RangeConstrainedVariableParams<std::uint8_t>{.min = 0, .max = 255, .def = 120};






constexpr auto kAccuracyRadius = RangeConstrainedVariableParams<std::uint8_t>{.min = 1, .max = 64, .def = 16};






constexpr auto kHitchance = RangeConstrainedVariableParams<std::uint8_t>{.min = 0, .max = 100, .def = 0};





constexpr auto kAutowallMaxThickness = RangeConstrainedVariableParams<std::uint8_t>{.min = 1, .max = 40, .def = 8};




constexpr auto kHoldKey = RangeConstrainedVariableParams<std::uint8_t>{.min = 0, .max = static_cast<std::uint8_t>(Bind::kLast), .def = Bind::kMouse5};

}
