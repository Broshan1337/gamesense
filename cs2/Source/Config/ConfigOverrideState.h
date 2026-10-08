#pragma once

#include <cstddef>
#include <type_traits>

namespace config_overrides {
// Bind storage is a fixed array whose elements remain alive until module unload.
// Keep saving the base value while the live config carries a temporary override.
inline constexpr std::size_t kCapacity = 1024;
struct SavedValue {
    const bool* active{};
    const double* original{};
};
inline SavedValue values[kCapacity]{};
inline void (*restoreBeforeLoad)() = nullptr;

inline void registerValue(std::size_t index, const bool* active, const double* original) noexcept
{
    if (index < kCapacity)
        values[index] = {active, original};
}

template <typename Number>
[[nodiscard]] Number valueForSave(std::size_t index, Number current) noexcept
{
    if constexpr (std::is_arithmetic_v<Number>) {
        if (index < kCapacity) {
            const auto& value = values[index];
            if (value.active && *value.active && value.original)
                return static_cast<Number>(*value.original);
        }
    }
    return current;
}

inline void restore() noexcept
{
    if (restoreBeforeLoad)
        restoreBeforeLoad();
}
}
