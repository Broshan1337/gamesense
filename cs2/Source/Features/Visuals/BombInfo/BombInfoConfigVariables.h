#pragma once

#include <Config/ConfigVariable.h>
#include <Config/RangeConstrainedVariableParams.h>
#include <Utils/ColorUtils.h>

namespace bomb_info_params
{
inline constexpr auto kSafeZoneRadius = RangeConstrainedVariableParams<std::uint16_t>{.min = 100, .max = 2000, .def = 600};
}

namespace bomb_info_vars
{
CONFIG_VARIABLE(Enabled, bool, false);
CONFIG_VARIABLE(InfoPanel, bool, true);
CONFIG_VARIABLE(SafeZone, bool, true);
CONFIG_VARIABLE_RANGE(SafeZoneRadius, bomb_info_params::kSafeZoneRadius);
CONFIG_VARIABLE(SafeZoneColor, color::Rgba, (color::Rgba{255, 130, 130, 60}));
CONFIG_VARIABLE(SiteColorA, color::Rgba, (color::Rgba{120, 180, 255, 230}));
}
