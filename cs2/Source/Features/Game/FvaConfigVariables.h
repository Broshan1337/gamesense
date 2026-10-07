#pragma once

#include <Config/ConfigVariable.h>
#include <Config/RangeConstrainedVariableParams.h>







CONFIG_VARIABLE(FvaEnabled, bool, true);




CONFIG_VARIABLE(FvaZeroOriginSpoof, bool, false);




CONFIG_VARIABLE(FvaReseedChains, bool, false);






CONFIG_VARIABLE(FvaSilentShots, bool, false);




CONFIG_VARIABLE(FvaChainOnFireOnly, bool, false);

namespace fva_params
{




constexpr auto kSubsteps = RangeConstrainedVariableParams<std::uint8_t>{.min = 1, .max = 15, .def = 8};
} 

namespace fva_vars
{
CONFIG_VARIABLE_RANGE(Substeps, fva_params::kSubsteps);
} 
