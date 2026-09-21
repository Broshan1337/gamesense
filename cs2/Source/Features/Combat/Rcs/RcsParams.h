#pragma once

#include <cstdint>
#include <Config/RangeConstrainedVariableParams.h>

namespace rcs_params
{

// How hard to cancel the recoil (aim punch), as a percentage of the punch we read. The game applies
// the punch to the shot/view through its own scale factors (the getter returns a "base" punch that
// the fire path multiplies), so 100% of what we read does NOT equal the real climb - it under-
// compensates. The range therefore goes past 100 so the value can be dialed up until the spray pattern
// flattens; ~200-250 is the expected sweet spot. 100 leaves it clearly under-compensated (was the old
// default). Max is 255 (the uint8 slider ceiling).
constexpr auto kStrength = RangeConstrainedVariableParams<std::uint8_t>{.min = 0, .max = 255, .def = 200};

}
