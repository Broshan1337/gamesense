#pragma once
#include <cstdint>
#include <Config/ConfigVariable.h>
#include <Config/RangeConstrainedVariableParams.h>
namespace animation_mod_vars {
CONFIG_VARIABLE(Freeze, bool, false);
CONFIG_VARIABLE(DisableIK, bool, false);
CONFIG_VARIABLE(DisableRagdolls, bool, false);
CONFIG_VARIABLE(ModifyRagdollScale, bool, false);
CONFIG_VARIABLE_RANGE(RagdollScale, (RangeConstrainedVariableParams<float>{0.1f, 5.0f, 1.0f}));
CONFIG_VARIABLE(AnimateViewmodel, bool, false);
CONFIG_VARIABLE_RANGE(ViewmodelSpinSpeed, (RangeConstrainedVariableParams<float>{-1080.0f, 1080.0f, 180.0f}));
CONFIG_VARIABLE_RANGE(ViewmodelPitchSway, (RangeConstrainedVariableParams<float>{0.0f, 180.0f, 30.0f}));
}
