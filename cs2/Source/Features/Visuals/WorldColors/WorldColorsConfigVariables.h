#pragma once

#include <cstdint>

#include <Config/ConfigVariable.h>
#include <Config/RangeConstrainedVariableParams.h>

CONFIG_VARIABLE(WorldColorsInfernoEnabled, bool, false);
CONFIG_VARIABLE(MolotovColor, color::Rgba, (color::Rgba{255, 120, 20, 255}));
CONFIG_VARIABLE(IncendiaryColor, color::Rgba, (color::Rgba{40, 120, 255, 255}));
CONFIG_VARIABLE(WorldColorsLightsEnabled, bool, false);
CONFIG_VARIABLE(WorldColorsLightColor, color::Rgba, (color::Rgba{255, 255, 255, 255}));
CONFIG_VARIABLE(WorldColorsSkyEnabled, bool, false);
CONFIG_VARIABLE(WorldColorsSkyColor, color::Rgba, (color::Rgba{120, 170, 255, 255}));
CONFIG_VARIABLE(WorldColorsWorldEnabled, bool, false);
CONFIG_VARIABLE(WorldColorsWorldColor, color::Rgba, (color::Rgba{150, 150, 150, 255}));
// Braced initializer inside CONFIG_VARIABLE_RANGE would split the macro on the commas - named
// params constant is the project pattern (triggerbot_params::kHitchance).
namespace world_colors_params
{
inline constexpr auto kFogDensity = RangeConstrainedVariableParams<std::uint8_t>{.min = 0, .max = 100, .def = 70};
// Fog end distance in units. The map fog start (typically ~500) is owned by the game's fog
// controller and our write to it is overwritten every frame - end distance is NOT, so the
// visible fog strength is controlled through (start..end] with end close to start = dense.
inline constexpr auto kFogDistance = RangeConstrainedVariableParams<float>{.min = 600.0f, .max = 20000.0f, .def = 1500.0f};
// Bloom strength percent; the cvar value written is strength * 0.05 (1% -> 0.05, 100% -> 5.0,
// the game's default sits around 0.15). uint8 per the schema's range-branch rule.
inline constexpr auto kBloomStrength = RangeConstrainedVariableParams<std::uint8_t>{.min = 1, .max = 100, .def = 40};
// Sky HDR overbright: multiplies the tint channels written into the sky scene objects. 1.0 =
// plain color; values above 1.0 push the sky over 1.0 luminance, which is what makes it pass
// the bloom threshold and GLOW (the bloom strength cvar only scales pixels that already
// qualify). Float - the range exceeds uint8 and floats are schema/test-supported.
inline constexpr auto kSkyBrightness = RangeConstrainedVariableParams<float>{.min = 1.0f, .max = 10.0f, .def = 1.0f};
}

CONFIG_VARIABLE(WorldColorsFogEnabled, bool, false);

// Sky Bloom (r_csgo_render_post_bloom_strength override): scales the game's own post-process
// bloom pass, which is what makes bright areas - the recolored sky above all - glow. 0 = off
// (the cvar keeps its default). Paired with Recolor Sky this is the full "sunrise bloom" look.
CONFIG_VARIABLE(WorldColorsBloomEnabled, bool, false);
CONFIG_VARIABLE_RANGE(WorldColorsBloomStrength, world_colors_params::kBloomStrength);
CONFIG_VARIABLE_RANGE(WorldColorsSkyBrightness, world_colors_params::kSkyBrightness);
CONFIG_VARIABLE(WorldColorsFogColor, color::Rgba, (color::Rgba{10, 14, 28, 255}));
CONFIG_VARIABLE_RANGE(WorldColorsFogDensity, world_colors_params::kFogDensity);
CONFIG_VARIABLE_RANGE(WorldColorsFogDistance, world_colors_params::kFogDistance);
