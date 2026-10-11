#pragma once

#include <Config/ConfigVariable.h>
#include <Utils/ColorUtils.h>

CONFIG_VARIABLE(BulletTracersEnabled, bool, false);
CONFIG_VARIABLE(BulletTracersColor, color::Rgba, (color::Rgba{120, 180, 255, 255}));