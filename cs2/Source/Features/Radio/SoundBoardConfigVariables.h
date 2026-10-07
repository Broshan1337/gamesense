#pragma once

#include <Config/ConfigVariable.h>
#include <Config/RangeConstrainedVariableParams.h>
#include <GameClient/Bind.h>

#include <cstdint>

namespace soundboard_params
{



inline constexpr auto kVoiceFormat = RangeConstrainedVariableParams<std::uint8_t>{.min = 0, .max = 2, .def = 2};




inline constexpr auto kClipIndex = RangeConstrainedVariableParams<std::uint8_t>{.min = 0, .max = 63, .def = 0};

inline constexpr auto kSoundKeyBind = RangeConstrainedVariableParams<std::uint8_t>{.min = 0, .max = static_cast<std::uint8_t>(Bind::kLast), .def = 0};
}

namespace soundboard_vars
{











CONFIG_VARIABLE_RANGE(ClipIndex, soundboard_params::kClipIndex);
CONFIG_VARIABLE_RANGE(VoiceFormat, soundboard_params::kVoiceFormat);
CONFIG_VARIABLE_RANGE(SoundKeyBind, soundboard_params::kSoundKeyBind);

}