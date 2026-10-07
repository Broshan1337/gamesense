#pragma once

#include <cstdint>

#include <CS2/Classes/IGameEventManager2.h>
#include <Vmt/VmtLengthCalculator.h>
#include <Vmt/VmtSwapper.h>

bool GameEventManagerHook_onFireEventClientSide(cs2::IGameEventManager2* thisptr, cs2::IGameEvent* event) noexcept;

// Hooks CGameEventManager::FireEventClientSide (vtable slot 9).
//
// 2026-10-06 ROOT-CAUSE FIX. Until today this hook received the address of a global and treated
// *global as the event manager. That global was NOT the event manager - it is the
// Source2EngineToClient001 interface instance (its only writer in libclient.so is
// `lea rdi,"Source2EngineToClient001"; call rbx; mov [global],rax`), so the hook was
// VMT-swapping the ENGINE-CLIENT interface and hooking ITS slot 9. Consequences, all matching
// the crash history documented below: every "slot 9" dispatch was an engine->client call, the
// hook body read a non-event pointer through the game_events:: accessors, and every
// event-driven feature starved. The old comments ("engine-side event manager singleton",
// "engine2 keeps its own global at engine2+0x9F60C8", the 256-slot floor) were rationalizations
// of that wrong object - the real CGameEventManager's composite lives in libclient and the
// shared libclient-built VmtLengthCalculator scans it fine.
//
// The real manager is a placement-constructed static inside libclient.so (RTTI-confirmed
// _ZTI17CGameEventManager), resolved by pattern to the object's own address - hence this class
// stores the instance directly (same reasoning as Source2ClientHook; there is no
// pointer-to-pointer to track). It is NOT obtainable via CreateInterface: libclient's interface
// list has exactly 8 static registrations and GAMEEVENTSMANAGER002 is not one of them (walked
// from the exported CreateInterface's registry head on 1.41.8.8 and 1.41.8.9).
//
// Slot 9 verified on 1.41.8.9: slot 9 = 0x16F1AC0 = `mov ecx,1; xor edx,edx; jmp
// <core dispatcher>` - the FireEventClientSide thin forwarder (matches the IGameEventManager2.h
// derivation; Itanium destructor slots shift the Windows index by one, as everywhere else).
class GameEventManagerHook {
public:
    GameEventManagerHook(cs2::IGameEventManager2* gameEventManager, const VmtLengthCalculator& vmtLengthCalculator) noexcept
        : gameEventManager{gameEventManager}
        , vmtLengthCalculator{vmtLengthCalculator}
    {
    }

    [[nodiscard]] cs2::IGameEventManager2::FireEventClientSide* getOriginalFireEventClientSide() const noexcept
    {
        return originalFireEventClientSide;
    }

    void uninstall() const noexcept
    {
        if (gameEventManager)
            hook.uninstall(*reinterpret_cast<std::uintptr_t**>(gameEventManager));
    }

    [[nodiscard]] bool isInstalled() const noexcept
    {
        return hook.wasEverInstalled() && gameEventManager && hook.isInstalled(*reinterpret_cast<std::uintptr_t**>(gameEventManager));
    }

    void install() noexcept
    {
        // minSlots: the composite measures ~170 slots (the historical floor was sized from the
        // real manager's 167-slot composite - that number was correct, it was the OBJECT that
        // was wrong). The shared calculator scans libclient sections, which now actually cover
        // this vtable, so the scan finds the true length and 170 is just the floor guarantee
        // for the hooked slot.
        if (gameEventManager && hook.install(vmtLengthCalculator, *reinterpret_cast<std::uintptr_t**>(gameEventManager), 170)) {
            originalFireEventClientSide = hook.hook(9, &GameEventManagerHook_onFireEventClientSide);
        }
    }

    cs2::IGameEventManager2* gameEventManager;
    VmtLengthCalculator vmtLengthCalculator;
    VmtSwapper hook;
    cs2::IGameEventManager2::FireEventClientSide* originalFireEventClientSide{ nullptr };
};
