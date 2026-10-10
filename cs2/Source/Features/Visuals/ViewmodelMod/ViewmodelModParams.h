#pragma once

#include <cstdint>
#include <Config/RangeConstrainedVariableParams.h>

namespace viewmodel_mod_params
{

constexpr auto kFov = RangeConstrainedVariableParams<std::uint8_t>{.min = 40, .max = 90, .def = 80};
constexpr auto kPreviewFallbackFov = 54.0f;


// The engine clamps viewmodel_offset_x/y/z to [-2, 2] - the old -10..10 ranges were a lie:
// the sliders offered values the cvars silently saturate at 2 ("won't go further past a
// number"). Keep the sliders honest: -2..2 is the full real range.
constexpr auto kOffsetX = RangeConstrainedVariableParams<float>{.min = -2.0f, .max = 2.0f, .def = 1.0f};
constexpr auto kOffsetY = RangeConstrainedVariableParams<float>{.min = -2.0f, .max = 2.0f, .def = 1.0f};
constexpr auto kOffsetZ = RangeConstrainedVariableParams<float>{.min = -2.0f, .max = 2.0f, .def = -1.0f};

constexpr auto kRotation = RangeConstrainedVariableParams<float>{.min = -180.0f, .max = 180.0f, .def = 0.0f};

}
