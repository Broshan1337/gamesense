#pragma once

#include <cstdint>
#include <Config/RangeConstrainedVariableParams.h>

namespace rcs_params
{







constexpr auto kStrength = RangeConstrainedVariableParams<std::uint8_t>{.min = 0, .max = 255, .def = 200};

}
