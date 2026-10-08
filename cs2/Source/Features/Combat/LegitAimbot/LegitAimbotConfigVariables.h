#pragma once

#include <Config/ConfigVariable.h>
#include <Features/Combat/TargetSelection.h>
#include "LegitAimbotParams.h"

namespace legit_aimbot_vars
{




CONFIG_VARIABLE(Enabled, bool, false);
CONFIG_VARIABLE_RANGE(TargetSelection, target_selection::kMode);
CONFIG_VARIABLE(TargetLock, bool, true);
CONFIG_VARIABLE(WallCheck, bool, true);

CONFIG_VARIABLE_RANGE(AimKey, legit_aimbot_params::kAimKey);
CONFIG_VARIABLE_RANGE(Fov, legit_aimbot_params::kFov);
CONFIG_VARIABLE_RANGE(Smooth, legit_aimbot_params::kSmooth);





CONFIG_VARIABLE(DrawFov, bool, false);



CONFIG_VARIABLE(FovCircleColor, color::Rgba, (color::Rgba{0, 255, 85, 255}));





CONFIG_VARIABLE(SpreadCircleFov, bool, false);



CONFIG_VARIABLE(HitHead, bool, true);
CONFIG_VARIABLE(HitChest, bool, true);
CONFIG_VARIABLE(HitStomach, bool, false);
CONFIG_VARIABLE(HitArms, bool, false);
CONFIG_VARIABLE(HitLegs, bool, false);

CONFIG_VARIABLE_RANGE(Mode, (RangeConstrainedVariableParams<std::uint8_t>{0, 2, 0}));
CONFIG_VARIABLE(VisibleAim, bool, true);
CONFIG_VARIABLE(AlwaysOn, bool, false);
CONFIG_VARIABLE(OnlyWhileFiring, bool, false);
CONFIG_VARIABLE(RequireMouseMovement, bool, false);
CONFIG_VARIABLE(IgnoreFlash, bool, true);
CONFIG_VARIABLE(RecoilCompensation, bool, true);
CONFIG_VARIABLE_RANGE(Strength, (RangeConstrainedVariableParams<std::uint8_t>{1, 100, 65}));
CONFIG_VARIABLE_RANGE(Deadzone, (RangeConstrainedVariableParams<float>{0.0f, 3.0f, 0.05f}));
CONFIG_VARIABLE_RANGE(MaxSpeed, (RangeConstrainedVariableParams<float>{1.0f, 720.0f, 180.0f}));
CONFIG_VARIABLE_RANGE(ReactionMs, (RangeConstrainedVariableParams<std::uint16_t>{0, 500, 0}));
CONFIG_VARIABLE_RANGE(SwitchDelayMs, (RangeConstrainedVariableParams<std::uint16_t>{0, 500, 120}));
CONFIG_VARIABLE_RANGE(SnapFov, (RangeConstrainedVariableParams<float>{0.1f, 30.0f, 2.0f}));

}
