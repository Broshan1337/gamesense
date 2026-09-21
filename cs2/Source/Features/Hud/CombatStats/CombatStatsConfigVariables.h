#pragma once

#include <Config/ConfigVariable.h>
#include <Config/RangeConstrainedVariableParams.h>

namespace combat_stats_vars
{
// Combat stats HUD (hit counters + hit feed). The offsets shift the widgets from their default
// anchors (counters: bottom-left above the status chips; feed: top-left under the radar) in HUD
// pixels - the watermark slider pattern. The feed auto-hides each line after FeedLifetime s.
inline constexpr auto kOffsetRange = RangeConstrainedVariableParams<std::uint8_t>{.min = 0, .max = 120, .def = 0};
inline constexpr auto kFeedLifetimeRange = RangeConstrainedVariableParams<std::uint8_t>{.min = 1, .max = 30, .def = 6};
// Counter box offsets: float range so the box can live anywhere on modern screens - the box is
// mouse-draggable in game and the drag writes the same offsets (the player list's pattern).
inline constexpr auto kCounterOffsetRange = RangeConstrainedVariableParams<float>{.min = 0.0f, .max = 4096.0f, .def = 0.0f};
CONFIG_VARIABLE_RANGE(FeedLifetime, kFeedLifetimeRange);
CONFIG_VARIABLE_RANGE(FeedOffsetX, kOffsetRange);
CONFIG_VARIABLE_RANGE(FeedOffsetY, kOffsetRange);
CONFIG_VARIABLE_RANGE(CountersOffsetX, kCounterOffsetRange);
CONFIG_VARIABLE_RANGE(CountersOffsetY, kCounterOffsetRange);
}
