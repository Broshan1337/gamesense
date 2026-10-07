#pragma once

#include <cstdint>
#include <Config/RangeConstrainedVariableParams.h>

namespace radio_params
{



constexpr auto kVolume = RangeConstrainedVariableParams<std::uint8_t>{.min = 0, .max = 100, .def = 50};

}
