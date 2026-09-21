#pragma once

#include <cstdint>

#include <Config/ConfigVariable.h>
#include <Config/RangeConstrainedVariableParams.h>

// Glitch text generator (glyphy/symboldb-style zalgo + fixed cursed recipes; Misc > GLITCH
// TEXT). Style picks the random combining-mark pool family, Intensity scales marks/glyph.
// 6 = HEAVY: the glyphy "heavy version" wall - classic + extended mark ranges stacked 10-60
// per glyph (intensity-scaled); the readable base text stays a thin line inside the wall.
// Preset > 0 = a FIXED recipe from the symboldb cursed set (same stack on every glyph,
// deterministic - "Bottom Whisper", the U+0336 walls, ...); 0 = Off (Style/Intensity path).
namespace glitch_gen_vars
{
constexpr auto kStyle = RangeConstrainedVariableParams<std::uint8_t>{.min = 0, .max = 6, .def = 0};
constexpr auto kIntensity = RangeConstrainedVariableParams<std::uint8_t>{.min = 0, .max = 100, .def = 60};
constexpr auto kPreset = RangeConstrainedVariableParams<std::uint8_t>{.min = 0, .max = 20, .def = 0};

CONFIG_VARIABLE_RANGE(Style, kStyle);
CONFIG_VARIABLE_RANGE(Intensity, kIntensity);
CONFIG_VARIABLE_RANGE(Preset, kPreset);
}
