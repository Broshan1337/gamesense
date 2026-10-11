#pragma once

#include <Config/ConfigVariable.h>
#include <Config/RangeConstrainedVariableParams.h>
#include <Utils/ColorUtils.h>

namespace esp_params
{
inline constexpr auto kBoxStyle = RangeConstrainedVariableParams<std::uint8_t>{.min = 0, .max = 1, .def = 0};
inline constexpr auto kViewLineLength = RangeConstrainedVariableParams<std::uint8_t>{.min = 0, .max = 100, .def = 50};
inline constexpr auto kMaxDistance = RangeConstrainedVariableParams<std::uint16_t>{.min = 0, .max = 5000, .def = 2500};
}

namespace esp_vars
{
CONFIG_VARIABLE(Enabled, bool, false);

CONFIG_VARIABLE(BoxEnabled, bool, true);
CONFIG_VARIABLE_RANGE(BoxStyle, esp_params::kBoxStyle);
CONFIG_VARIABLE(BoxFill, bool, false);
CONFIG_VARIABLE(BoxColor, color::Rgba, (color::Rgba{255, 255, 255, 220}));

CONFIG_VARIABLE(SkeletonEnabled, bool, true);
CONFIG_VARIABLE(SkeletonColor, color::Rgba, (color::Rgba{255, 255, 255, 200}));

CONFIG_VARIABLE(HeadCircleEnabled, bool, true);
CONFIG_VARIABLE(HeadCircleColor, color::Rgba, (color::Rgba{255, 255, 255, 230}));

CONFIG_VARIABLE(ViewLineEnabled, bool, false);
CONFIG_VARIABLE_RANGE(ViewLineLength, esp_params::kViewLineLength);
CONFIG_VARIABLE(SnaplineEnabled, bool, false);
CONFIG_VARIABLE(SnaplineColor, color::Rgba, (color::Rgba{255, 255, 255, 140}));

CONFIG_VARIABLE(HealthBarEnabled, bool, true);
CONFIG_VARIABLE(ArmorBarEnabled, bool, true);
CONFIG_VARIABLE(BackupHealthAnim, bool, true);

CONFIG_VARIABLE(NameEnabled, bool, false);
CONFIG_VARIABLE(WeaponNameEnabled, bool, false);
CONFIG_VARIABLE(DistanceEnabled, bool, true);

CONFIG_VARIABLE(FlagsEnabled, bool, true);
CONFIG_VARIABLE(FlagFlashed, bool, true);
CONFIG_VARIABLE(FlagScoped, bool, true);
CONFIG_VARIABLE(FlagReloading, bool, true);
CONFIG_VARIABLE(FlagDefusing, bool, true);
CONFIG_VARIABLE(FlagBomb, bool, true);
CONFIG_VARIABLE(FlagKit, bool, true);

CONFIG_VARIABLE(TeammatesEnabled, bool, false);
CONFIG_VARIABLE_RANGE(MaxDistance, esp_params::kMaxDistance);

// Customization batch (2026-10-12): sizes/colors as requested - all appended
// LAST (order-sensitive config loader).
inline constexpr auto kHeadCircleRadius = RangeConstrainedVariableParams<std::uint8_t>{.min = 2, .max = 20, .def = 5};
inline constexpr auto kBoxThickness = RangeConstrainedVariableParams<std::uint8_t>{.min = 1, .max = 6, .def = 1};
inline constexpr auto kCornerLength = RangeConstrainedVariableParams<std::uint8_t>{.min = 5, .max = 90, .def = 25};
inline constexpr auto kNameSize = RangeConstrainedVariableParams<std::uint8_t>{.min = 8, .max = 24, .def = 12};
inline constexpr auto kHealthBarWidth = RangeConstrainedVariableParams<std::uint8_t>{.min = 1, .max = 10, .def = 3};

CONFIG_VARIABLE_RANGE(HeadCircleRadius, kHeadCircleRadius);
CONFIG_VARIABLE(HealthColor, color::Rgba, (color::Rgba{120, 255, 111, 190}));
CONFIG_VARIABLE(ArmorColor, color::Rgba, (color::Rgba{180, 184, 227, 230}));
CONFIG_VARIABLE_RANGE(BoxThickness, kBoxThickness);
CONFIG_VARIABLE_RANGE(CornerLength, kCornerLength);
CONFIG_VARIABLE_RANGE(NameSize, kNameSize);
CONFIG_VARIABLE_RANGE(HealthBarWidth, kHealthBarWidth);
inline constexpr auto kEchoSize = RangeConstrainedVariableParams<std::uint8_t>{.min = 8, .max = 28, .def = 13};
CONFIG_VARIABLE_RANGE(EchoSize, kEchoSize);
CONFIG_VARIABLE(FlagMoney, bool, true);
inline constexpr auto kFont = RangeConstrainedVariableParams<std::uint8_t>{.min = 0, .max = 1, .def = 0};
CONFIG_VARIABLE_RANGE(Font, kFont);
}