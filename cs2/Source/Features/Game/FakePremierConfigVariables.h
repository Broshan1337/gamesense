#pragma once

#include <Config/ConfigVariable.h>
#include "FakePremierParams.h"

CONFIG_VARIABLE(FakePremierEnabled, bool, false);
CONFIG_VARIABLE_RANGE(FakePremierScore, fake_premier_params::kScore);
