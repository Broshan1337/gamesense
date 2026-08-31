#pragma once

#include <cstdint>

#include <Config/ConfigVariable.h>
#include <Config/RangeConstrainedVariableParams.h>
#include <GameClient/Bind.h>

namespace panic_params
{
// Bind that toggles the combat panic state (GameClient/Bind.h encoding; 0 = Off). Captured
// CS2-settings style in the UI. Default Caps Lock so the feature is usable out of the box; set
// to Off to disable the key entirely.
constexpr auto kPanicKey = RangeConstrainedVariableParams<std::uint8_t>{.min = 0, .max = static_cast<std::uint8_t>(Bind::kLast), .def = 57 /* Caps Lock */};
}

namespace panic_vars
{
// Toggling the bound key on freezes every combat feature (aimbots, triggerbot, RCS, movement
// assists) until toggled back - an instant failsafe if something behaves wrong mid-match.
CONFIG_VARIABLE_RANGE(Bind, panic_params::kPanicKey);
}
