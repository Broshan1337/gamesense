#pragma once

#include <Config/ConfigVariable.h>
#include <Config/RangeConstrainedVariableParams.h>

#include <cstdint>

namespace userinfo_flood_params
{

inline constexpr auto kSendsPerTick = RangeConstrainedVariableParams<std::uint8_t>{.min = 1, .max = 64, .def = 4};

inline constexpr auto kEveryTicks = RangeConstrainedVariableParams<std::uint8_t>{.min = 1, .max = 32, .def = 1};
}

namespace userinfo_flood_vars
{

















CONFIG_VARIABLE(Enabled, bool, false);
CONFIG_VARIABLE(HeavyMode, bool, false);








CONFIG_VARIABLE(DirectMode, bool, false);
CONFIG_VARIABLE(FloodName, bool, false);
CONFIG_VARIABLE(FloodClutch, bool, false);
CONFIG_VARIABLE(FloodTeamColor, bool, false);
CONFIG_VARIABLE(FloodXhStyle, bool, false);
CONFIG_VARIABLE(FloodXhColor, bool, false);
CONFIG_VARIABLE(FloodXhSize, bool, false);
CONFIG_VARIABLE(FloodXhGap, bool, false);
CONFIG_VARIABLE(FloodXhThick, bool, false);
CONFIG_VARIABLE(FloodXhOutline, bool, false);
CONFIG_VARIABLE(FloodXhDot, bool, false);
CONFIG_VARIABLE(FloodXhAlpha, bool, false);
CONFIG_VARIABLE(FloodXhSniper, bool, false);
CONFIG_VARIABLE(FloodLoadout, bool, false);
CONFIG_VARIABLE(FloodTeamId, bool, false);
CONFIG_VARIABLE(FloodXhColorR, bool, false);
CONFIG_VARIABLE(FloodXhColorG, bool, false);
CONFIG_VARIABLE(FloodXhColorB, bool, false);
CONFIG_VARIABLE_RANGE(SendsPerTick, userinfo_flood_params::kSendsPerTick);
CONFIG_VARIABLE_RANGE(EveryTicks, userinfo_flood_params::kEveryTicks);

}