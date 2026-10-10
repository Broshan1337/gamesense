#pragma once

#include <cstdint>
#include <Config/RangeConstrainedVariableParams.h>

namespace fake_premier_params
{



constexpr auto kScore = RangeConstrainedVariableParams<std::uint16_t>{.min = 1, .max = 30000, .def = 20000};

// CS2 gates the premier skill group behind >= 10 competitive wins - spoof the win count
// alongside the score or the rank stays hidden.
constexpr auto kWins = RangeConstrainedVariableParams<std::uint8_t>{.min = 0, .max = 100, .def = 10};

// Wingman skill group: the CS:GO-style ladder id (Silver I .. The Global Elite).
constexpr auto kWingmanRank = RangeConstrainedVariableParams<std::uint8_t>{.min = 0, .max = 18, .def = 18};

}
