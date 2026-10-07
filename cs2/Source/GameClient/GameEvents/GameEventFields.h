#pragma once

#include <cstdint>
#include <cstring>

#include <CS2/Classes/CUtlStringToken.h>
#include <CS2/Classes/IGameEventManager2.h>
#include <Utils/RetAddrSpoofer.h>





namespace game_events
{





[[nodiscard]] inline const char* name(cs2::IGameEvent* event) noexcept
{
    if (!event)
        return nullptr;

    const auto vtable = *reinterpret_cast<void* const* const*>(event);
    if (!vtable)
        return nullptr;

    const auto getName = vtable[cs2::GameEventAccessors::kGetNameVtableSlot];
    if (!getName)
        return nullptr;

    return RetAddrSpoofer::spoof(reinterpret_cast<cs2::GameEventAccessors::GetName*>(getName))(event);
}

[[nodiscard]] inline bool is(cs2::IGameEvent* event, const char* eventName) noexcept
{
    const auto actualName = name(event);
    return actualName && std::strcmp(actualName, eventName) == 0;
}




[[nodiscard]] inline std::int64_t entityForKey(cs2::IGameEvent* event, const char* key) noexcept
{
    if (!event)
        return -1;

    const auto vtable = *reinterpret_cast<void* const* const*>(event);
    if (!vtable)
        return -1;

    const auto getEntityForKey = vtable[cs2::GameEventAccessors::kGetEntityForKeyVtableSlot];
    if (!getEntityForKey)
        return -1;

    const cs2::CUtlStringToken keyToken{key};
    return RetAddrSpoofer::spoof(reinterpret_cast<cs2::GameEventAccessors::GetEntityForKey*>(getEntityForKey))(event, &keyToken, -1);
}















[[nodiscard]] inline int intForKey(cs2::IGameEvent* event, const char* key, int defaultValue = 0) noexcept
{
    if (!event)
        return defaultValue;

    const auto vtable = *reinterpret_cast<void* const* const*>(event);
    if (!vtable)
        return defaultValue;

    constexpr int kGetIntVtableSlot = 8;
    const auto getInt = vtable[kGetIntVtableSlot];
    if (!getInt)
        return defaultValue;

    using GetInt = int(cs2::IGameEvent*, const cs2::CUtlStringToken*, int);
    const cs2::CUtlStringToken keyToken{key};
    return RetAddrSpoofer::spoof(reinterpret_cast<GetInt*>(getInt))(event, &keyToken, defaultValue);
}







[[nodiscard]] inline const char* stringForKey(cs2::IGameEvent* event, const char* key, const char* defaultValue = nullptr) noexcept
{
    if (!event)
        return defaultValue;

    const auto vtable = *reinterpret_cast<void* const* const*>(event);
    if (!vtable)
        return defaultValue;

    constexpr int kGetStringVtableSlot = 11;
    const auto getString = vtable[kGetStringVtableSlot];
    if (!getString)
        return defaultValue;

    using GetString = const char*(cs2::IGameEvent*, const cs2::CUtlStringToken*, const char*);
    const cs2::CUtlStringToken keyToken{key};
    return RetAddrSpoofer::spoof(reinterpret_cast<GetString*>(getString))(event, &keyToken, defaultValue);
}



[[nodiscard]] inline float floatForKey(cs2::IGameEvent* event, const char* key, float defaultValue = 0.0f) noexcept
{
    if (!event)
        return defaultValue;

    const auto vtable = *reinterpret_cast<void* const* const*>(event);
    if (!vtable)
        return defaultValue;

    constexpr int kGetFloatVtableSlot = 10;
    const auto getFloat = vtable[kGetFloatVtableSlot];
    if (!getFloat)
        return defaultValue;

    using GetFloat = float(cs2::IGameEvent*, const cs2::CUtlStringToken*, float);
    const cs2::CUtlStringToken keyToken{key};
    return RetAddrSpoofer::spoof(reinterpret_cast<GetFloat*>(getFloat))(event, &keyToken, defaultValue);
}



template <typename HookContext>
[[nodiscard]] bool localPlayerIsSlot(HookContext& hookContext, std::int64_t slot) noexcept
{
    constexpr std::int64_t kMaxPlayerSlot = 63;
    if (slot < 0 || slot > kMaxPlayerSlot)
        return false;

    const auto localControllerIndex = hookContext.localPlayerController().baseEntity().handle().index().value;
    if (localControllerIndex <= 0)
        return false;

    return slot == localControllerIndex - 1;
}
















template <typename HookContext>
[[nodiscard]] bool localPlayerIsAttacker(HookContext& hookContext, cs2::IGameEvent* event) noexcept
{
    if (!event)
        return false;

    const auto vtable = *reinterpret_cast<void* const* const*>(event);
    if (!vtable)
        return false;

    const auto getEntityForKey = vtable[cs2::GameEventAccessors::kGetEntityForKeyVtableSlot];
    if (!getEntityForKey)
        return false;

    const cs2::CUtlStringToken attackerKey{"attacker"};
    const auto attackerSlot = RetAddrSpoofer::spoof(reinterpret_cast<cs2::GameEventAccessors::GetEntityForKey*>(getEntityForKey))(event, &attackerKey, -1);

    
    
    constexpr std::int64_t kMaxPlayerSlot = 63;
    if (attackerSlot < 0 || attackerSlot > kMaxPlayerSlot)
        return false;

    const auto localControllerIndex = hookContext.localPlayerController().baseEntity().handle().index().value;
    if (localControllerIndex <= 0)
        return false;

    return attackerSlot == localControllerIndex - 1;
}

}
