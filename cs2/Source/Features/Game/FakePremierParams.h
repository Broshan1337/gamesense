#pragma once

#include <cstdint>
#include <Config/RangeConstrainedVariableParams.h>

namespace fake_premier_params
{

// Premier ratings live roughly 1000-30000. Defaulting to a mid-high rating because that is what
// the feature is for - anything else is a deliberate choice the slider exists to make.
constexpr auto kScore = RangeConstrainedVariableParams<std::uint16_t>{.min = 1, .max = 30000, .def = 20000};

}
