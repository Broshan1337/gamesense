#pragma once

#include <cstdint>
#include <Config/RangeConstrainedVariableParams.h>

namespace aimbot_params
{






constexpr float kMaxFov = 180.0f;




constexpr auto kExtrapolateTicks = RangeConstrainedVariableParams<std::uint8_t>{.min = 0, .max = 16, .def = 2};

// kHitchance / kMinDamage / kPointScale moved to RageGroupsConfigVariables.h
// (per-weapon-group config).



constexpr auto kBacktrackTicks = RangeConstrainedVariableParams<std::uint8_t>{.min = 1, .max = 16, .def = 12};



constexpr auto kForceShotWaitTicks = RangeConstrainedVariableParams<std::uint8_t>{.min = 1, .max = 34, .def = 34};



constexpr auto kPointScale = RangeConstrainedVariableParams<std::uint8_t>{.min = 0, .max = 100, .def = 85};

}
