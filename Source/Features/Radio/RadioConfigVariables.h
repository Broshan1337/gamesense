#pragma once

#include <Config/ConfigVariable.h>
#include "RadioParams.h"

namespace radio_vars
{

// Web radio (Radio tab). Volume percentage handed to ffplay. Play/Stop are one-shot actions (the
// `radio` command verb), not config vars - only the volume persists.
CONFIG_VARIABLE_RANGE(Volume, radio_params::kVolume);

}
