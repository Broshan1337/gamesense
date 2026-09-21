#pragma once

#include <cstdint>
#include <Config/RangeConstrainedVariableParams.h>

namespace fake_commends_params
{

// Public commend counters (leader / teacher / friendly). Real accounts accumulate these slowly,
// so a four-digit number is the plausible ceiling the slider exists to exceed.
constexpr auto kCommends = RangeConstrainedVariableParams<std::uint16_t>{.min = 0, .max = 10000, .def = 1000};

}
