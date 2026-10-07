#pragma once

#include <cstdint>
#include <Config/RangeConstrainedVariableParams.h>

namespace fake_level_params
{



constexpr auto kLevel = RangeConstrainedVariableParams<std::uint8_t>{.min = 1, .max = 40, .def = 40};



constexpr auto kXp = RangeConstrainedVariableParams<std::uint16_t>{.min = 0, .max = 4999, .def = 0};

}
