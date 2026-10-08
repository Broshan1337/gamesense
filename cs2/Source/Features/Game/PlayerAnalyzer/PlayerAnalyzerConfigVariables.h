#pragma once

#include <cstdint>

#include <Config/ConfigVariable.h>
#include <Config/RangeConstrainedVariableParams.h>







namespace analyzer_params
{



inline constexpr auto kSnapThresholdParams = RangeConstrainedVariableParams<std::uint8_t>{.min = 5, .max = 90, .def = 25};

inline constexpr auto kCalloutThreshold = RangeConstrainedVariableParams<std::uint8_t>{.min = 10, .max = 100, .def = 50};
}

namespace analyzer_vars
{


CONFIG_VARIABLE(Enabled, bool, false);

CONFIG_VARIABLE(EspTag, bool, true);
CONFIG_VARIABLE_RANGE(SnapThreshold, analyzer_params::kSnapThresholdParams);


CONFIG_VARIABLE(VoiceProbe, bool, false);



CONFIG_VARIABLE(VoiceLog, bool, false);




CONFIG_VARIABLE(Callout, bool, false);
CONFIG_VARIABLE(CalloutTeamChat, bool, false);

CONFIG_VARIABLE_RANGE(CalloutThreshold, analyzer_params::kCalloutThreshold);

}
