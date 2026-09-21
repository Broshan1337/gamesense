#pragma once

#include <cstdint>
#include <Config/RangeConstrainedVariableParams.h>

namespace fake_level_params
{

// CS2's profile rank runs 1 to 40. Defaulting to the top because that is what the feature is for -
// anything lower is a deliberate choice the slider exists to make.
constexpr auto kLevel = RangeConstrainedVariableParams<std::uint8_t>{.min = 1, .max = 40, .def = 40};

// XP within the level: CS2 levels are 5000 XP each, so the bar reads 0..4999 inside a level
// (5000 would render as the next level's empty bar).
constexpr auto kXp = RangeConstrainedVariableParams<std::uint16_t>{.min = 0, .max = 4999, .def = 0};

}
