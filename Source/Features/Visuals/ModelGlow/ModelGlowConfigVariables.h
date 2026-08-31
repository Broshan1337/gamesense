#pragma once

#include <cstdint>

#include <Config/ConfigVariable.h>
#include <Utils/ColorUtils.h>
#include <Utils/IdentityMacro.h>
#include <Utils/InRange.h>
#include "ModelGlowParams.h"

namespace model_glow_vars
{

CONFIG_VARIABLE(Enabled, bool, false);
CONFIG_VARIABLE(GlowPlayers, bool, true);
CONFIG_VARIABLE(GlowOnlyEnemies, bool, true);
CONFIG_VARIABLE(GlowWeapons, bool, true);
CONFIG_VARIABLE(GlowDroppedBomb, bool, true);
CONFIG_VARIABLE(GlowTickingBomb, bool, true);
CONFIG_VARIABLE(GlowDefuseKits, bool, true);
CONFIG_VARIABLE(GlowGrenadeProjectiles, bool, true);

// Enemy/Ally glow colors are full RGBA colors (picked with the color picker in the UI), not hues.
// The model glow writes the color as a scene-object Color3 attribute, so the alpha channel has no
// in-game effect; it is kept so both glow pickers look and behave the same.
CONFIG_VARIABLE(AllyColor, color::Rgba, (color::Rgba{0, 255, 0, 255}));
CONFIG_VARIABLE(EnemyColor, color::Rgba, (color::Rgba{255, 0, 0, 255}));

CONFIG_VARIABLE_HUE(MolotovHue, model_glow_params::kMolotovHue);
CONFIG_VARIABLE_HUE(FlashbangHue, model_glow_params::kFlashbangHue);
CONFIG_VARIABLE_HUE(HEGrenadeHue, model_glow_params::kHEGrenadeHue);
CONFIG_VARIABLE_HUE(SmokeGrenadeHue, model_glow_params::kSmokeGrenadeHue);

CONFIG_VARIABLE_HUE(DroppedBombHue, model_glow_params::kDroppedBombHue);
CONFIG_VARIABLE_HUE(TickingBombHue, model_glow_params::kTickingBombHue);
CONFIG_VARIABLE_HUE(DefuseKitHue, model_glow_params::kDefuseKitHue);

}
