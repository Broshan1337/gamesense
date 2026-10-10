#pragma once

#include <Config/ConfigVariable.h>
#include "FakePremierParams.h"

CONFIG_VARIABLE(FakePremierEnabled, bool, false);
CONFIG_VARIABLE_RANGE(FakePremierScore, fake_premier_params::kScore);
CONFIG_VARIABLE(FakePremierWins, bool, false);
CONFIG_VARIABLE_RANGE(PremierWins, fake_premier_params::kWins);
CONFIG_VARIABLE(FakeWingmanEnabled, bool, false);
CONFIG_VARIABLE_RANGE(WingmanRank, fake_premier_params::kWingmanRank);
