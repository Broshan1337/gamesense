#pragma once

#include <Config/ConfigVariable.h>
#include <Config/RangeConstrainedVariableParams.h>
#include <Utils/ColorUtils.h>

namespace radar_params
{
inline constexpr auto kZoom = RangeConstrainedVariableParams<std::uint8_t>{.min = 2, .max = 40, .def = 12};
inline constexpr auto kSize = RangeConstrainedVariableParams<std::uint8_t>{.min = 80, .max = 250, .def = 180};
}

namespace radar_vars
{
CONFIG_VARIABLE(Enabled, bool, false);
CONFIG_VARIABLE_RANGE(Zoom, radar_params::kZoom);
CONFIG_VARIABLE_RANGE(Size, radar_params::kSize);
CONFIG_VARIABLE(EnemyColor, color::Rgba, (color::Rgba{255, 90, 90, 230}));
CONFIG_VARIABLE(TeamColor, color::Rgba, (color::Rgba{120, 180, 255, 230}));
CONFIG_VARIABLE(BombColor, color::Rgba, (color::Rgba{255, 170, 60, 230}));
CONFIG_VARIABLE(ViewCones, bool, true);
}
