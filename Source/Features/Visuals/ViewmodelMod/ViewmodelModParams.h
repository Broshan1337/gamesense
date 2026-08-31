#pragma once

#include <cstdint>
#include <Config/RangeConstrainedVariableParams.h>

namespace viewmodel_mod_params
{

constexpr auto kFov = RangeConstrainedVariableParams<std::uint8_t>{.min = 40, .max = 90, .def = 80};
constexpr auto kPreviewFallbackFov = 54.0f;

// Viewmodel offset ranges in cvar units; defaults = the game's stock viewmodel_offset values.
constexpr auto kOffsetX = RangeConstrainedVariableParams<float>{.min = -10.0f, .max = 10.0f, .def = 1.0f};
constexpr auto kOffsetY = RangeConstrainedVariableParams<float>{.min = -10.0f, .max = 10.0f, .def = 1.0f};
constexpr auto kOffsetZ = RangeConstrainedVariableParams<float>{.min = -10.0f, .max = 10.0f, .def = -1.0f};

}
