#pragma once

#include <cstdint>
#include <Config/RangeConstrainedVariableParams.h>

namespace radio_params
{

// Web radio playback volume, as a percentage. Passed to ffplay's -volume on play. (Phase 1 applies it
// on the next Play; live change comes later.)
constexpr auto kVolume = RangeConstrainedVariableParams<std::uint8_t>{.min = 0, .max = 100, .def = 50};

}
