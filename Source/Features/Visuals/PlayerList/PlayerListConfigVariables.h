#pragma once

#include <Config/ConfigVariable.h>
#include <Config/RangeConstrainedVariableParams.h>

CONFIG_VARIABLE(PlayerListEnabled, bool, false);

namespace player_list_params
{
// Window position (range-constrained floats - the schema/test-supported shape), persisted so
// the list always comes back where it was left. The draw side seeds the window with these on
// first use and writes the position back while a menu drag moves it (the embedded ImGui keeps
// no ini file, so the config is the only persistence).
inline constexpr auto kPlayerListPosX = RangeConstrainedVariableParams<float>{.min = 0.0f, .max = 8192.0f, .def = 10.0f};
inline constexpr auto kPlayerListPosY = RangeConstrainedVariableParams<float>{.min = 0.0f, .max = 8192.0f, .def = 64.0f};
}

CONFIG_VARIABLE_RANGE(PlayerListPosX, player_list_params::kPlayerListPosX);
CONFIG_VARIABLE_RANGE(PlayerListPosY, player_list_params::kPlayerListPosY);
