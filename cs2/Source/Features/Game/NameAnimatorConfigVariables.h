#pragma once

#include <Config/ConfigVariable.h>
#include <Config/RangeConstrainedVariableParams.h>

#include <cstdint>

namespace name_animator_params
{













inline constexpr auto kMode = RangeConstrainedVariableParams<std::uint8_t>{.min = 0, .max = 22, .def = 0};



inline constexpr auto kSpeed = RangeConstrainedVariableParams<std::uint8_t>{.min = 2, .max = 64, .def = 8};
}

namespace name_animator_vars
{




CONFIG_VARIABLE(Enabled, bool, false);
CONFIG_VARIABLE_RANGE(Mode, name_animator_params::kMode);
CONFIG_VARIABLE_RANGE(Speed, name_animator_params::kSpeed);






CONFIG_VARIABLE(DirectSend, bool, false);

}
