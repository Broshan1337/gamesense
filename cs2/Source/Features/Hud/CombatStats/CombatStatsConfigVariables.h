#pragma once

#include <Config/ConfigVariable.h>
#include <Config/RangeConstrainedVariableParams.h>

namespace combat_stats_vars
{



inline constexpr auto kOffsetRange = RangeConstrainedVariableParams<std::uint8_t>{.min = 0, .max = 120, .def = 0};
inline constexpr auto kFeedLifetimeRange = RangeConstrainedVariableParams<std::uint8_t>{.min = 1, .max = 30, .def = 6};


inline constexpr auto kCounterOffsetRange = RangeConstrainedVariableParams<float>{.min = 0.0f, .max = 4096.0f, .def = 0.0f};
CONFIG_VARIABLE_RANGE(FeedLifetime, kFeedLifetimeRange);
CONFIG_VARIABLE_RANGE(FeedOffsetX, kOffsetRange);
CONFIG_VARIABLE_RANGE(FeedOffsetY, kOffsetRange);
CONFIG_VARIABLE_RANGE(CountersOffsetX, kCounterOffsetRange);
CONFIG_VARIABLE_RANGE(CountersOffsetY, kCounterOffsetRange);
}
