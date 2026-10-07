#pragma once

#include <Config/ConfigVariable.h>
#include <Config/RangeConstrainedVariableParams.h>

#include <cstdint>

#include <GameClient/Bind.h>

namespace server_lagger_params
{




inline constexpr auto kMsgsPerBatch = RangeConstrainedVariableParams<std::uint8_t>{.min = 1, .max = 65, .def = 65};


inline constexpr auto kAudioKB = RangeConstrainedVariableParams<std::uint8_t>{.min = 1, .max = 15, .def = 1};




inline constexpr auto kAmount = RangeConstrainedVariableParams<std::uint16_t>{.min = 1, .max = 2000, .def = 14};






inline constexpr auto kPayloadMode = RangeConstrainedVariableParams<std::uint8_t>{.min = 0, .max = 4, .def = 0};





inline constexpr auto kMeterOffsetRange = RangeConstrainedVariableParams<float>{.min = -4096.0f, .max = 4096.0f, .def = 0.0f};



inline constexpr auto kFreezeTicks = RangeConstrainedVariableParams<std::uint8_t>{.min = 1, .max = 128, .def = 25};

inline constexpr auto kPreset = RangeConstrainedVariableParams<std::uint8_t>{.min = 0, .max = 3, .def = 0};



inline constexpr auto kRampInterval = RangeConstrainedVariableParams<std::uint8_t>{.min = 1, .max = 10, .def = 2};

inline constexpr auto kLaggerBind = RangeConstrainedVariableParams<std::uint8_t>{.min = 0, .max = static_cast<std::uint8_t>(Bind::kLast), .def = 0};



inline constexpr auto kPulseOn = RangeConstrainedVariableParams<std::uint8_t>{.min = 1, .max = 32, .def = 8};
inline constexpr auto kPulseOff = RangeConstrainedVariableParams<std::uint8_t>{.min = 1, .max = 64, .def = 8};
}

namespace server_lagger_vars
{








CONFIG_VARIABLE(Enabled, bool, false);
CONFIG_VARIABLE_RANGE(MsgsPerBatch, server_lagger_params::kMsgsPerBatch);
CONFIG_VARIABLE_RANGE(AudioKB, server_lagger_params::kAudioKB);
CONFIG_VARIABLE_RANGE(Amount, server_lagger_params::kAmount);
CONFIG_VARIABLE_RANGE(PayloadMode, server_lagger_params::kPayloadMode);




CONFIG_VARIABLE(MeterEnabled, bool, false);
CONFIG_VARIABLE_RANGE(MeterOffsetX, server_lagger_params::kMeterOffsetRange);
CONFIG_VARIABLE_RANGE(MeterOffsetY, server_lagger_params::kMeterOffsetRange);






CONFIG_VARIABLE_RANGE(Preset, server_lagger_params::kPreset);



CONFIG_VARIABLE(DatagramMode, bool, false);


CONFIG_VARIABLE(LoopFreeze, bool, false);
CONFIG_VARIABLE_RANGE(FreezeTicks, server_lagger_params::kFreezeTicks);


CONFIG_VARIABLE(AutoStop, bool, false);


CONFIG_VARIABLE(SlowRamp, bool, false);
CONFIG_VARIABLE_RANGE(RampInterval, server_lagger_params::kRampInterval);
CONFIG_VARIABLE_RANGE(LaggerKey, server_lagger_params::kLaggerBind);


CONFIG_VARIABLE(NumPackets, bool, false);


CONFIG_VARIABLE(PulseMode, bool, false);
CONFIG_VARIABLE_RANGE(PulseOn, server_lagger_params::kPulseOn);
CONFIG_VARIABLE_RANGE(PulseOff, server_lagger_params::kPulseOff);

CONFIG_VARIABLE(MixMode, bool, false);


CONFIG_VARIABLE(PopulationScale, bool, false);


CONFIG_VARIABLE(Misattribute, bool, false);

}