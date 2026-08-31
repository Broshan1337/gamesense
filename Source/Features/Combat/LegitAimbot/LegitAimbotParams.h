#pragma once

#include <cstdint>
#include <Config/RangeConstrainedVariableParams.h>
#include <GameClient/Bind.h>

namespace legit_aimbot_params
{

// Field of view, in degrees, the legit aimbot is allowed to pull across. Kept small by default (a legit
// assist only nudges onto targets already near the crosshair, unlike the rage FOV which can be huge).
constexpr auto kFov = RangeConstrainedVariableParams<std::uint8_t>{.min = 0, .max = 180, .def = 5};

// Smoothing divisor: each tick the view closes 1/smooth of the remaining angle to the target. 1 = snap
// (instant), higher = slower, more human-looking approach. Never 0 (that would divide by zero).
constexpr auto kSmooth = RangeConstrainedVariableParams<std::uint8_t>{.min = 1, .max = 20, .def = 5};

// Aim key: which bind activates the assist while held (GameClient/Bind.h encoding; 0 = Off).
// Captured CS2-settings style in the UI. Default MOUSE5 - the forward thumb button this feature
// was hardcoded to before binds became configurable.
constexpr auto kAimKey = RangeConstrainedVariableParams<std::uint8_t>{.min = 0, .max = static_cast<std::uint8_t>(Bind::kLast), .def = Bind::kMouse5};

}
