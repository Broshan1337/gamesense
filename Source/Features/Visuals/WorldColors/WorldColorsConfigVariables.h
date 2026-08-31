#pragma once

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
}

CONFIG_VARIABLE(WorldColorsFogEnabled, bool, false);
CONFIG_VARIABLE(WorldColorsFogColor, color::Rgba, (color::Rgba{10, 14, 28, 255}));
CONFIG_VARIABLE_RANGE(WorldColorsFogDensity, world_colors_params::kFogDensity);
CONFIG_VARIABLE_RANGE(WorldColorsFogDistance, world_colors_params::kFogDistance);
