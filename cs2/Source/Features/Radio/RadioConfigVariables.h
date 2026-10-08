#pragma once

#include <Config/ConfigVariable.h>
#include <Config/RangeConstrainedVariableParams.h>
#include <GameClient/Bind.h>

#include "RadioParams.h"

namespace radio_params
{
inline constexpr auto kVoiceKey = RangeConstrainedVariableParams<std::uint8_t>{.min = 0, .max = static_cast<std::uint8_t>(Bind::kLast), .def = 0};
}

namespace radio_vars
{



CONFIG_VARIABLE_RANGE(Volume, radio_params::kVolume);







CONFIG_VARIABLE(MicBroadcast, bool, false);




CONFIG_VARIABLE_RANGE(VoiceKeyBind, radio_params::kVoiceKey);






CONFIG_VARIABLE(AirhornEnabled, bool, false);
CONFIG_VARIABLE(AirhornFirstBlood, bool, true);
CONFIG_VARIABLE(AirhornHeadshot, bool, false);
CONFIG_VARIABLE(AirhornRoundWin, bool, false);




CONFIG_VARIABLE(ShowNowPlaying, bool, true);




CONFIG_VARIABLE(ShowMediaPlayers, bool, true);







inline constexpr auto kOffsetRange = RangeConstrainedVariableParams<float>{.min = -4096.0f, .max = 4096.0f, .def = 0.0f};
CONFIG_VARIABLE_RANGE(NowPlayingOffsetX, kOffsetRange);
CONFIG_VARIABLE_RANGE(NowPlayingOffsetY, kOffsetRange);

}
