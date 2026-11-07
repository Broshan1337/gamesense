#pragma once

#include <cstdint>

#include <Config/ConfigVariable.h>
#include <Config/RangeConstrainedVariableParams.h>
#include <GameClient/Bind.h>

namespace freecam_params
{

inline constexpr auto kSpeed = RangeConstrainedVariableParams<float>{.min = 5.0f, .max = 1000.0f, .def = 250.0f};
inline constexpr auto kKeyBind = RangeConstrainedVariableParams<std::uint8_t>{.min = 0, .max = static_cast<std::uint8_t>(Bind::kLast), .def = 0};

}

namespace freecam_vars
{

CONFIG_VARIABLE(Enabled, bool, false);
CONFIG_VARIABLE_RANGE(Speed, freecam_params::kSpeed);
CONFIG_VARIABLE_RANGE(KeyBind, freecam_params::kKeyBind);

}
