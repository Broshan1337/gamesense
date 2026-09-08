#pragma once

#include <Config/ConfigVariable.h>
#include <Utils/ColorUtils.h>

namespace chams_vars
{

// Enemy chams (skeet parity, phase-2 overlay form): every enemy pawn's mesh primitives get a
// duplicate render pass tinted with EnemyColor. Off by default.
CONFIG_VARIABLE(Enabled, bool, false);

CONFIG_VARIABLE(EnemyColor, color::Rgba, (color::Rgba{255, 40, 40, 200}));

}
