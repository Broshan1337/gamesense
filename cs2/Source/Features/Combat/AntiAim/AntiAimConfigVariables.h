#pragma once

#include <Config/ConfigVariable.h>
#include <Config/RangeConstrainedVariableParams.h>
#include <GameClient/Bind.h>
#include <Utils/ColorUtils.h>

namespace antiaim_params
{
// pitch mode: 0 = off, 1 = down (89), 2 = up (-89)
inline constexpr auto kPitchMode = RangeConstrainedVariableParams<std::uint8_t>{.min = 0, .max = 2, .def = 1};
// autoyaw mode: 0 = none, 1 = crosshair, 2 = distance, 3 = health
inline constexpr auto kAutoYaw = RangeConstrainedVariableParams<std::uint8_t>{.min = 0, .max = 3, .def = 0};
inline constexpr auto kKey = RangeConstrainedVariableParams<std::uint8_t>{.min = 0, .max = static_cast<std::uint8_t>(Bind::kLast), .def = Bind::kOff};
}

namespace antiaim_vars
{
CONFIG_VARIABLE(Enabled, bool, false);
CONFIG_VARIABLE_RANGE(PitchMode, antiaim_params::kPitchMode);
CONFIG_VARIABLE_RANGE(AtTargetsKey, antiaim_params::kKey);
CONFIG_VARIABLE(AutoYawAdjust, bool, true);
CONFIG_VARIABLE_RANGE(ManualLeftKey, antiaim_params::kKey);
CONFIG_VARIABLE_RANGE(ManualRightKey, antiaim_params::kKey);
CONFIG_VARIABLE_RANGE(AutoYaw, antiaim_params::kAutoYaw);
CONFIG_VARIABLE(AvoidBackstab, bool, true);
CONFIG_VARIABLE(DirectionIndicator, bool, true);
CONFIG_VARIABLE(DirectionIndicatorColor, color::Rgba, (color::Rgba{173, 192, 255, 220}));
CONFIG_VARIABLE(HideShots, bool, true);
}