#pragma once

#include <atomic>
#include <optional>

#include <Utils/ManuallyDestructible.h> 
#include <GlobalContext/GlobalContext.h>
#include <GlobalContext/HookQuiesce.h>










namespace ui_config
{

template <typename ConfigVariable>
[[nodiscard]] std::optional<typename ConfigVariable::ValueType> tryGet() noexcept
{
    if (!HookContext<GlobalContext>::isGlobalContextComplete() || HookQuiesce::isShuttingDown())
        return std::nullopt;
    HookContext<GlobalContext> hookContext;
    return hookContext.config().template getVariable<ConfigVariable>();
}

template <typename ConfigVariable>
[[nodiscard]] typename ConfigVariable::ValueType get() noexcept
{
    if (const auto value = tryGet<ConfigVariable>())
        return *value;
    return ConfigVariable::kDefaultValue;
}




inline std::atomic<std::uint64_t> changeEpoch{0};

template <typename ConfigVariable>
bool set(typename ConfigVariable::ValueType newValue) noexcept
{
    if (!HookContext<GlobalContext>::isGlobalContextComplete() || HookQuiesce::isShuttingDown())
        return false;
    HookContext<GlobalContext> hookContext;
    const bool changed = hookContext.config().template setVariable<ConfigVariable>(newValue);
    if (changed)
        changeEpoch.fetch_add(1, std::memory_order_relaxed);
    return changed;
}



template <typename Functor>
[[nodiscard]] bool withContext(Functor&& functor) noexcept
{
    if (!HookContext<GlobalContext>::isGlobalContextComplete() || HookQuiesce::isShuttingDown())
        return false;
    HookContext<GlobalContext> hookContext;
    functor(hookContext);
    return true;
}



template <typename Functor>
bool withConfig(Functor&& functor) noexcept
{
    if (!HookContext<GlobalContext>::isGlobalContextComplete() || HookQuiesce::isShuttingDown())
        return false;
    HookContext<GlobalContext> hookContext;
    functor(hookContext.config());
    return true;
}

[[nodiscard]] inline std::uint8_t listedConfigCount() noexcept
{
    std::uint8_t count = 0;
    withConfig([&](auto&& config) { count = config.listedConfigCount(); });
    return count;
}

[[nodiscard]] inline const char* listedConfigName(std::uint8_t index) noexcept
{
    const char* name = "";
    withConfig([&](auto&& config) { name = config.listedConfigName(index); });
    return name;
}

[[nodiscard]] inline std::uint8_t activeConfigIndex() noexcept
{
    std::uint8_t index = 0;
    withConfig([&](auto&& config) { index = config.activeConfigIndex(); });
    return index;
}

[[nodiscard]] inline const char* activeConfigNameForDisplay() noexcept
{
    const char* name = "";
    withConfig([&](auto&& config) { name = config.activeConfigNameForDisplay(); });
    return name;
}

inline void switchToConfig(std::uint8_t listedIndex) noexcept
{
    withConfig([&](auto&& config) { config.switchToConfig(listedIndex); });
}

inline void saveActive() noexcept
{
    withConfig([&](auto&& config) { config.saveActive(); });
}

[[nodiscard]] inline bool createAndSwitchToConfig(const char* name) noexcept
{
    bool ok = false;
    withConfig([&](auto&& config) { ok = config.createAndSwitchToConfig(name); });
    return ok;
}

inline void restoreDefaults() noexcept
{
    withConfig([&](auto&& config) { config.restoreDefaults(); });
}

[[nodiscard]] inline bool duplicateActiveConfig() noexcept
{
    bool ok = false;
    withConfig([&](auto&& config) { ok = config.duplicateActiveConfig(); });
    return ok;
}

[[nodiscard]] inline bool deleteConfig(std::uint8_t listedIndex) noexcept
{
    bool ok = false;
    withConfig([&](auto&& config) { ok = config.deleteListedConfig(listedIndex); });
    return ok;
}

[[nodiscard]] inline bool renameActiveConfig(const char* newName) noexcept
{
    bool ok = false;
    withConfig([&](auto&& config) { ok = config.renameActiveConfig(newName); });
    return ok;
}

}
