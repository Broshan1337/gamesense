#pragma once

#include <Config/ConfigVariable.h>
#include <Config/RangeConstrainedVariableParams.h>
#include <GameClient/Bind.h>

#include <cstdint>

namespace net_lag_params
{

inline constexpr auto kChokeKeyBind = RangeConstrainedVariableParams<std::uint8_t>{.min = 0, .max = static_cast<std::uint8_t>(Bind::kLast), .def = 0};
inline constexpr auto kChokeTicks = RangeConstrainedVariableParams<std::uint8_t>{.min = 1, .max = 16, .def = 4};
inline constexpr auto kBlipCount = RangeConstrainedVariableParams<std::uint8_t>{.min = 0, .max = 8, .def = 2};
inline constexpr auto kDupCount = RangeConstrainedVariableParams<std::uint8_t>{.min = 0, .max = 4, .def = 0};
inline constexpr auto kDelayMs = RangeConstrainedVariableParams<std::uint8_t>{.min = 5, .max = 100, .def = 50};




inline constexpr auto kFloodBurstCount = RangeConstrainedVariableParams<std::uint8_t>{.min = 0, .max = 32, .def = 0};
inline constexpr auto kFloodKeyBind = RangeConstrainedVariableParams<std::uint8_t>{.min = 0, .max = static_cast<std::uint8_t>(Bind::kLast), .def = 0};






inline constexpr auto kConnlessFloodCount = RangeConstrainedVariableParams<std::uint8_t>{.min = 0, .max = 64, .def = 0};
inline constexpr auto kConnlessKeyBind = RangeConstrainedVariableParams<std::uint8_t>{.min = 0, .max = static_cast<std::uint8_t>(Bind::kLast), .def = 0};
}

























namespace net_lag_vars
{
CONFIG_VARIABLE(Enabled, bool, false);
CONFIG_VARIABLE(FakelagAlways, bool, false);
CONFIG_VARIABLE_RANGE(ChokeKeyBind, net_lag_params::kChokeKeyBind);
CONFIG_VARIABLE_RANGE(ChokeTicks, net_lag_params::kChokeTicks);
CONFIG_VARIABLE_RANGE(BlipCount, net_lag_params::kBlipCount);
CONFIG_VARIABLE_RANGE(DupCount, net_lag_params::kDupCount);
CONFIG_VARIABLE(DelayEnabled, bool, false);
CONFIG_VARIABLE_RANGE(DelayMs, net_lag_params::kDelayMs);
CONFIG_VARIABLE(StatsEnabled, bool, false);
CONFIG_VARIABLE_RANGE(FloodBurstCount, net_lag_params::kFloodBurstCount);

CONFIG_VARIABLE_RANGE(FloodKeyBind, net_lag_params::kFloodKeyBind);
CONFIG_VARIABLE_RANGE(ConnlessFloodCount, net_lag_params::kConnlessFloodCount);

CONFIG_VARIABLE_RANGE(ConnlessKeyBind, net_lag_params::kConnlessKeyBind);
}
