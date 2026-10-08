#pragma once

#include <cstdint>

#include <Config/ConfigVariable.h>
#include <Config/RangeConstrainedVariableParams.h>
#include <GameClient/Bind.h>

namespace panic_params
{



constexpr auto kPanicKey = RangeConstrainedVariableParams<std::uint8_t>{.min = 0, .max = static_cast<std::uint8_t>(Bind::kLast), .def = 57 };
}

namespace panic_vars
{


CONFIG_VARIABLE_RANGE(Bind, panic_params::kPanicKey);
}
