#pragma once

#include <Config/ConfigVariable.h>
#include <Config/RangeConstrainedVariableParams.h>

#include <GameClient/Bind.h>

#include <cstdint>

namespace movement_params
{

inline constexpr auto kSlowWalkSpeed = RangeConstrainedVariableParams<std::uint8_t>{.min = 1, .max = 100, .def = 33};
}





namespace movement_vars
{




CONFIG_VARIABLE(EdgeJump, bool, false);




CONFIG_VARIABLE(EdgeStop, bool, false);





CONFIG_VARIABLE(Desubtick, bool, false);



CONFIG_VARIABLE(SlowWalk, bool, false);
CONFIG_VARIABLE_RANGE(SlowWalkSpeed, movement_params::kSlowWalkSpeed);



CONFIG_VARIABLE(FastLadder, bool, false);





CONFIG_VARIABLE(JumpBug, bool, false);

}




namespace last_tick_vars
{
inline constexpr auto kDefuseKeyBind = RangeConstrainedVariableParams<std::uint8_t>{.min = 0, .max = static_cast<std::uint8_t>(Bind::kLast), .def = 0};

CONFIG_VARIABLE(Enabled, bool, false);
CONFIG_VARIABLE_RANGE(DefuseKey, last_tick_vars::kDefuseKeyBind);
}




namespace supertoss_vars
{
CONFIG_VARIABLE(Enabled, bool, false);
}
