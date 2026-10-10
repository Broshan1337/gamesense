#pragma once

#include <Config/ConfigVariable.h>

CONFIG_VARIABLE(KillEffectsEnabled, bool, false);
CONFIG_VARIABLE(KillEffectsColor, color::Rgba, (color::Rgba{170, 210, 255, 255}));
CONFIG_VARIABLE(KillEffectsScreenFlash, bool, true);