#pragma once

#include <Config/ConfigVariable.h>
#include "FakeCommendsParams.h"

CONFIG_VARIABLE(FakeCommendsEnabled, bool, false);
CONFIG_VARIABLE_RANGE(FakeCommendsFriendly, fake_commends_params::kCommends);
CONFIG_VARIABLE_RANGE(FakeCommendsTeaching, fake_commends_params::kCommends);
CONFIG_VARIABLE_RANGE(FakeCommendsLeader, fake_commends_params::kCommends);
