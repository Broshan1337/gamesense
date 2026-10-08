#pragma once

#include <Config/ConfigVariable.h>
#include <Config/RangeConstrainedVariableParams.h>
#include <cstdint>

CONFIG_VARIABLE(BunnyhopEnabled, bool, false);
CONFIG_VARIABLE(AutoStrafeEnabled, bool, false);
CONFIG_VARIABLE(TestStraferEnabled, bool, false);

CONFIG_VARIABLE_RANGE(AutoStrafeMode, (RangeConstrainedVariableParams<std::uint8_t>{0, 1, 1}));
CONFIG_VARIABLE_RANGE(LegitStrafeStrength, (RangeConstrainedVariableParams<std::uint8_t>{0, 100, 65}));
CONFIG_VARIABLE_RANGE(LegitStrafeMouseThreshold, (RangeConstrainedVariableParams<std::uint8_t>{1, 20, 2}));
