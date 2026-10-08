#pragma once

#include <cstdint>
#include <Config/RangeConstrainedVariableParams.h>

namespace fake_commends_params
{



constexpr auto kCommends = RangeConstrainedVariableParams<std::uint16_t>{.min = 0, .max = 10000, .def = 1000};

}
