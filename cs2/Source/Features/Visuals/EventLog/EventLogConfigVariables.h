#pragma once

#include <Config/ConfigVariable.h>

namespace event_log_vars
{
CONFIG_VARIABLE(Enabled, bool, false);
CONFIG_VARIABLE(LogHits, bool, true);
CONFIG_VARIABLE(LogMisses, bool, true);
CONFIG_VARIABLE(LogBomb, bool, true);
CONFIG_VARIABLE(LogRounds, bool, true);
CONFIG_VARIABLE(LogConnects, bool, true);
}
