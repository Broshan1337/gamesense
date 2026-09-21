#pragma once

#include <Config/ConfigVariable.h>
#include "FakeLevelParams.h"

CONFIG_VARIABLE(FakeLevelEnabled, bool, false);
CONFIG_VARIABLE_RANGE(FakeLevelValue, fake_level_params::kLevel);
CONFIG_VARIABLE_RANGE(FakeLevelXp, fake_level_params::kXp);
