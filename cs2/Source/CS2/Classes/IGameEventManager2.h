#pragma once

#include <cstdint>

#include "CUtlStringToken.h"

namespace cs2 {

// Opaque - nothing in this project reads IGameEvent's own fields yet (the hook just needs to
// pass the pointer through to the original function and, later, to per-feature handlers).
struct IGameEvent;

// The classic Source engine event-dispatch interface, still present in CS2's Source 2 client
// under its original name and interface-registry string ("GAMEEVENTSMANAGER002" - see
// GameEventManagerPointer.h). Concrete implementation is CGameEventManager; FireEventClientSide
// is vtable slot 9 there (confirmed via IDA MCP this session: found the real internal dispatcher
// via two of its own log/warning strings, then found the thin 2-argument public wrapper
// (sub_169DB30, `return coreDispatch(this, event, 0, 1)` - the `1` matches the exact boolean
// that gates the core dispatcher's "Game event ..." client-side log line, as opposed to the
// "Server event ..." branch taken when that flag is 0), then located that wrapper's address
// directly inside CGameEventManager's vtable at byte offset 0x48 past the vtable's own start).
struct IGameEventManager2 {
    using FireEventClientSide = bool(IGameEventManager2* thisptr, IGameEvent* event);
};

// Accessors on the event object itself (concrete class CGameEvent, whose vtable
// _ZTV10CGameEvent lives at 0x43ad3a0 in libclient.so - NOT in libengine2.so, despite the
// classic Source engine putting it there). Slot N is at vtableStart + 16 + 8*N.
//
// IMPORTANT when porting slot numbers from a Windows reference: they are all shifted by one.
// The Itanium ABI emits TWO destructor slots (D1/D0) at 0 and 1 where MSVC emits a single one,
// so Windows slot N is Linux slot N+1. This was verified, not assumed - see GetName below.
struct GameEventAccessors {
    // Vtable slot 2. Confirmed independently of the vtable dump: the client's core event
    // dispatcher (sub_169D5F0) reads exactly this slot to log event names, and guards it with a
    // devirtualization check against sub_1671750, which is precisely the address sitting in slot
    // 2 of _ZTV10CGameEvent. Two unrelated routes agreeing is what makes this trustworthy.
    static constexpr int kGetNameVtableSlot = 2;
    using GetName = const char*(IGameEvent* thisptr);

    // Vtable slot 13. Takes a hashed field key BY POINTER and returns that field decoded as an
    // entity value: it forwards to the generic get-by-key primitive (slot 8) and then applies
    // `if ((v & 0x4000) == 0) v &= 0x1FFFFFF;` - entity handle/index masking.
    //
    // Used here to read player_hurt's "attacker" field. This is NOT the pointer-returning
    // GetPlayerController a Windows reference would use; it returns a raw entity value, which is
    // actually more convenient - comparing indices avoids needing CCSPlayerController at all.
    // The exact encoding of the returned value is NOT fully understood, so the caller must treat
    // a mismatch as "not us" and stay silent rather than doing anything risky.
    static constexpr int kGetEntityForKeyVtableSlot = 13;
    using GetEntityForKey = std::int64_t(IGameEvent* thisptr, const CUtlStringToken* key, std::int64_t defaultValue);
};

}
