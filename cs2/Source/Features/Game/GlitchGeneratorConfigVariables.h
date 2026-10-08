#pragma once

#include <cstdint>

#include <Config/ConfigVariable.h>
#include <Config/RangeConstrainedVariableParams.h>







namespace glitch_gen_vars
{
constexpr auto kStyle = RangeConstrainedVariableParams<std::uint8_t>{.min = 0, .max = 6, .def = 0};
constexpr auto kIntensity = RangeConstrainedVariableParams<std::uint8_t>{.min = 0, .max = 100, .def = 60};
constexpr auto kPreset = RangeConstrainedVariableParams<std::uint8_t>{.min = 0, .max = 20, .def = 0};

CONFIG_VARIABLE_RANGE(Style, kStyle);
CONFIG_VARIABLE_RANGE(Intensity, kIntensity);
CONFIG_VARIABLE_RANGE(Preset, kPreset);
}
