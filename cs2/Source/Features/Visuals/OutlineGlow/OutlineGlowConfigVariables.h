#pragma once

#include <Config/ConfigVariable.h>
#include "OutlineGlowParams.h"

namespace outline_glow_vars
{

CONFIG_VARIABLE(Enabled, bool, false);
CONFIG_VARIABLE(GlowDefuseKits, bool, true);
CONFIG_VARIABLE(GlowDroppedBomb, bool, true);
CONFIG_VARIABLE(GlowGrenadeProjectiles, bool, true);
CONFIG_VARIABLE(GlowHostages, bool, true);
CONFIG_VARIABLE(GlowPlayers, bool, true);
CONFIG_VARIABLE(GlowOnlyEnemies, bool, true);
CONFIG_VARIABLE(GlowTickingBomb, bool, true);
CONFIG_VARIABLE(GlowWeapons, bool, true);

// Enemy/Ally glow colors are full RGBA colors (picked with the color picker in the UI), not hues.
// The alpha channel is the glow alpha; the default matches outline_glow_params::kGlowAlpha so that
// the default look is unchanged. Spawn-protected players keep the dimmed kImmunePlayerGlowAlpha.
CONFIG_VARIABLE(AllyColor, color::Rgba, (color::Rgba{0, 255, 0, outline_glow_params::kGlowAlpha}));
CONFIG_VARIABLE(EnemyColor, color::Rgba, (color::Rgba{255, 0, 0, outline_glow_params::kGlowAlpha}));

CONFIG_VARIABLE_HUE(MolotovHue, outline_glow_params::kMolotovHue);
CONFIG_VARIABLE_HUE(FlashbangHue, outline_glow_params::kFlashbangHue);
CONFIG_VARIABLE_HUE(HEGrenadeHue, outline_glow_params::kHEGrenadeHue);
CONFIG_VARIABLE_HUE(SmokeGrenadeHue, outline_glow_params::kSmokeGrenadeHue);

CONFIG_VARIABLE_HUE(DroppedBombHue, outline_glow_params::kDroppedBombHue);
CONFIG_VARIABLE_HUE(TickingBombHue, outline_glow_params::kTickingBombHue);
CONFIG_VARIABLE_HUE(DefuseKitHue, outline_glow_params::kDefuseKitHue);

CONFIG_VARIABLE_HUE(HostageHue, outline_glow_params::kHostageHue);

CONFIG_VARIABLE(FlashbangColor, color::Rgba, (color::Rgba{64, 131, 255, outline_glow_params::kGlowAlpha}));
CONFIG_VARIABLE(HEGrenadeColor, color::Rgba, (color::Rgba{255, 64, 64, outline_glow_params::kGlowAlpha}));
CONFIG_VARIABLE(SmokeGrenadeColor, color::Rgba, (color::Rgba{64, 255, 64, outline_glow_params::kGlowAlpha}));
CONFIG_VARIABLE(MolotovColor, color::Rgba, (color::Rgba{255, 128, 0, outline_glow_params::kGlowAlpha}));
CONFIG_VARIABLE(DroppedBombColor, color::Rgba, (color::Rgba{255, 213, 77, outline_glow_params::kGlowAlpha}));
CONFIG_VARIABLE(TickingBombColor, color::Rgba, (color::Rgba{255, 0, 0, outline_glow_params::kGlowAlpha}));
CONFIG_VARIABLE(DefuseKitColor, color::Rgba, (color::Rgba{0, 213, 255, outline_glow_params::kGlowAlpha}));
CONFIG_VARIABLE(HostageColor, color::Rgba, (color::Rgba{255, 200, 50, outline_glow_params::kGlowAlpha}));
}
