#pragma once

#include <cstdint>
#include <Config/RangeConstrainedVariableParams.h>

namespace target_selection {
enum class Mode : std::uint8_t {
    Crosshair,
    Distance,
    Health,
};

inline constexpr auto kMode = RangeConstrainedVariableParams<std::uint8_t>{.min = 0, .max = 2, .def = 0};
}
