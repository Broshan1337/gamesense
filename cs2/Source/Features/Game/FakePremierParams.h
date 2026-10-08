#pragma once

#include <cstdint>
#include <Config/RangeConstrainedVariableParams.h>

namespace fake_premier_params
{



constexpr auto kScore = RangeConstrainedVariableParams<std::uint16_t>{.min = 1, .max = 30000, .def = 20000};

}
