#pragma once

#include <cstdint>

#include <Config/ConfigVariable.h>
#include <Config/RangeConstrainedVariableParams.h>
#include <GameClient/Bind.h>






namespace chat_params
{

inline constexpr auto kIntelBindParams = RangeConstrainedVariableParams<std::uint8_t>{.min = 0, .max = static_cast<std::uint8_t>(Bind::kLast), .def = 0};

inline constexpr auto kRadioPhraseParams = RangeConstrainedVariableParams<std::uint8_t>{.min = 1, .max = 21, .def = 6};
}

namespace chat_vars
{


inline constexpr auto kSpamCountParams = RangeConstrainedVariableParams<std::uint8_t>{.min = 1, .max = 30, .def = 10};


inline constexpr auto kSpamIntervalParams = RangeConstrainedVariableParams<std::uint8_t>{.min = 2, .max = 100, .def = 10};



inline constexpr auto kWheelIntervalParams = RangeConstrainedVariableParams<std::uint8_t>{.min = 10, .max = 255, .def = 150};



inline constexpr auto kPingIntervalParams = RangeConstrainedVariableParams<std::uint8_t>{.min = 2, .max = 100, .def = 10};

inline constexpr auto kColorCycleSpeedParams = RangeConstrainedVariableParams<std::uint8_t>{.min = 1, .max = 64, .def = 64};

inline constexpr auto kNameCycleIntervalParams = RangeConstrainedVariableParams<std::uint8_t>{.min = 5, .max = 120, .def = 30};


inline constexpr auto kTheaterDelayParams = RangeConstrainedVariableParams<std::uint8_t>{.min = 3, .max = 30, .def = 8};

CONFIG_VARIABLE(SpamEnabled, bool, false);
CONFIG_VARIABLE_RANGE(SpamCount, kSpamCountParams);
CONFIG_VARIABLE_RANGE(SpamInterval, kSpamIntervalParams);

CONFIG_VARIABLE(WheelEnabled, bool, false);
CONFIG_VARIABLE_RANGE(RadioPhrase, chat_params::kRadioPhraseParams);
CONFIG_VARIABLE_RANGE(WheelInterval, kWheelIntervalParams);

CONFIG_VARIABLE(PingSpamEnabled, bool, false);
CONFIG_VARIABLE_RANGE(PingInterval, kPingIntervalParams);

CONFIG_VARIABLE(HudColorCycle, bool, false);
CONFIG_VARIABLE_RANGE(HudColorCycleSpeed, kColorCycleSpeedParams);

CONFIG_VARIABLE(NameCycleEnabled, bool, false);
CONFIG_VARIABLE_RANGE(NameCycleInterval, kNameCycleIntervalParams);

CONFIG_VARIABLE_RANGE(TheaterDelay, kTheaterDelayParams);



CONFIG_VARIABLE(IntelEnabled, bool, false);
CONFIG_VARIABLE_RANGE(IntelBind, chat_params::kIntelBindParams);



CONFIG_VARIABLE(StreakRadioEnabled, bool, false);



CONFIG_VARIABLE(LiveBadgeEnabled, bool, false);





inline constexpr auto kKickReasonParams = RangeConstrainedVariableParams<std::uint8_t>{.min = 1, .max = 255, .def = 163};
CONFIG_VARIABLE_RANGE(KickReason, kKickReasonParams);

CONFIG_VARIABLE_RANGE(KickKey, chat_params::kIntelBindParams);




CONFIG_VARIABLE(NameForceReconnect, bool, false);





CONFIG_VARIABLE(ClanTagEnabled, bool, false);






CONFIG_VARIABLE(ClanTagAnimateEnabled, bool, false);
inline constexpr auto kClanTagAnimateModeParams = RangeConstrainedVariableParams<std::uint8_t>{.min = 1, .max = 6, .def = 1};
CONFIG_VARIABLE_RANGE(ClanTagAnimateMode, kClanTagAnimateModeParams);
inline constexpr auto kClanTagAnimateSpeedParams = RangeConstrainedVariableParams<std::uint8_t>{.min = 1, .max = 64, .def = 32};
CONFIG_VARIABLE_RANGE(ClanTagAnimateSpeed, kClanTagAnimateSpeedParams);

}
