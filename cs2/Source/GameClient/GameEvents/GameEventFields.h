#pragma once

#include <cstdint>
#include <cstring>

#include <CS2/Classes/CUtlStringToken.h>
#include <CS2/Classes/IGameEventManager2.h>
#include <Utils/RetAddrSpoofer.h>

// Shared field decoding for anything hanging off the FireEventClientSide hook. Every such feature
// needs the same two questions answered - "which event is this?" and "did the local player cause
// it?" - and the second one was expensive to establish empirically (see localPlayerIsAttacker), so
// it lives here once instead of being copy-pasted per feature and drifting.
namespace game_events
{

// Calls CGameEvent's GetName through its vtable - see GameEventAccessors (IGameEventManager2.h)
// for how the slot was confirmed. Reading the event's embedded string directly would also work
// (the engine itself inlines that fast path) but depends on the object layout rather than just the
// slot, so the virtual call is the safer of the two.
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

// Reads a field as an entity value (vtable slot 13). For player_hurt's "attacker" and "userid"
// this yields a 0-BASED PLAYER SLOT - see localPlayerIsAttacker for how that was established.
// Returns the raw value; callers must range-check it themselves (65535 means "nobody").
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

// Reads a field as a plain integer: CGameEvent::GetInt, vtable slot 8.
//
// CONFIRMED by decompiling the whole typed-getter family (slots 7-12, all thin wrappers over
// distinct workers) and identifying them by how each converts the stored variant:
//   slot 7  -> takes a uint8 default          -> GetBool
//   slot 8  -> returns *(unsigned int*), and narrows a stored double via (int) -> GetInt   <-- this
//   slot 9  -> returns *(qword*)              -> GetUint64
//   slot 10 -> returns a float                -> GetFloat
//   slots 11/12                               -> the string/pointer pair
//
// The third argument is the value returned when the key is ABSENT (the worker's final
// `return defaultValue` when the lookup misses). It must be passed explicitly: omitting it does
// not default to zero, it leaves that register holding whatever happened to be in it, so a missing
// key would yield garbage rather than the intended fallback.
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

// Reads a field as a string: CGameEvent::GetString, vtable slot 11. The slot was identified in
// the same decompile pass as GetInt/GetFloat above (slots 7-12 are one family of thin typed
// wrappers; slots 11/12 are the string/pointer pair). 11 is the {hash, name-pointer} key variant
// - independently proven on the same Linux binaries by FrameworkCS2's GrenadeHelper, which reads
// game_newmap's "mapname" through exactly this slot and call shape (its whole per-map feature
// depends on it). Same third-argument rule as intForKey: the default MUST be passed explicitly.
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

// Reads a field as a float: CGameEvent::GetFloat, vtable slot 10 (identified in the same
// decompile pass as GetInt above - slot 10 returns a float and takes a float default).
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

// Shared "is this 0-based player slot the local player?" check (see localPlayerIsAttacker for how
// the slot encoding was established). bullet_impact's "userid" uses the same encoding.
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

// player_hurt's "attacker" comes back as a 0-BASED PLAYER SLOT, not an entity index.
//
// Determined from live data rather than assumed: across a full match the observed values were
// 0..9 on a 10-player server, plus 65535 for damage with no attacker (fall damage, world).
// The presence of a **zero** is what rules out an entity index - entity 0 is worldspawn and can
// never be a player - and the runs of `attacker=0` lined up exactly with the local player's own
// kills (each followed by player_death + gg_killed_enemy).
//
// Player controllers occupy entity indices 1..maxplayers in slot order, so the local slot is the
// local controller's entity index minus one. That matched the capture directly: the local
// controller was entity index 1, and the local player's hits reported attacker slot 0.
//
// Getting this wrong is not loud - the discarded entity-index reading would have compared against
// index 1 and silently fired on whichever player occupied slot 1. Change it only against fresh
// captured data, never by reasoning alone.
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

    // 65535 is the engine's "no attacker" marker; anything outside a plausible slot range is
    // rejected rather than trusted.
    constexpr std::int64_t kMaxPlayerSlot = 63;
    if (attackerSlot < 0 || attackerSlot > kMaxPlayerSlot)
        return false;

    const auto localControllerIndex = hookContext.localPlayerController().baseEntity().handle().index().value;
    if (localControllerIndex <= 0)
        return false;

    return attackerSlot == localControllerIndex - 1;
}

}
