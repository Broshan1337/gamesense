#pragma once

#include <cstdint>

#include <CS2/Classes/IGameEventManager2.h>
#include <Vmt/VmtLengthCalculator.h>
#include <Vmt/VmtSwapper.h>

bool GameEventManagerHook_onFireEventClientSide(cs2::IGameEventManager2* thisptr, cs2::IGameEvent* event) noexcept;

// CGameEventManager is a process-lifetime singleton, same reasoning as Source2ClientHook - the
// resolved pointer is stored directly rather than tracked through a live pointer-to-pointer.
class GameEventManagerHook {
public:
    GameEventManagerHook(cs2::IGameEventManager2** gameEventManagerGlobal, const VmtLengthCalculator& vmtLengthCalculator) noexcept
        : gameEventManagerGlobal{gameEventManagerGlobal}
        , vmtLengthCalculator{vmtLengthCalculator}
    {
    }

    [[nodiscard]] cs2::IGameEventManager2::FireEventClientSide* getOriginalFireEventClientSide() const noexcept
    {
        return originalFireEventClientSide;
    }

    // Dereferenced late, never cached at construction: the global is populated by the client's
    // own init, which is not guaranteed to have run by the time FullGlobalContext is built.
    [[nodiscard]] cs2::IGameEventManager2* getGameEventManager() const noexcept
    {
        return gameEventManagerGlobal ? *gameEventManagerGlobal : nullptr;
    }

    void uninstall() const noexcept
    {
        if (const auto gameEventManager = getGameEventManager())
            hook.uninstall(*reinterpret_cast<std::uintptr_t**>(gameEventManager));
    }

    [[nodiscard]] bool isInstalled() const noexcept
    {
        const auto gameEventManager = getGameEventManager();
        return hook.wasEverInstalled() && gameEventManager && hook.isInstalled(*reinterpret_cast<std::uintptr_t**>(gameEventManager));
    }

    void install() noexcept
    {
        // 09-26 RE-ENABLED: slot 9 verified (0x16f4680 = FireEventClientSide impl via the
        // shared listener-iteration). The 10:46/11:01 crashes were the FSN CLONE SIZE (37
        // slots on a 449-slot composite = pool overrun), not the GEM.
        const auto gameEventManager = getGameEventManager();
        // minSlots 256 (2026-09-27 fix for the 5x tier0-free crashes): the hooked object is
        // the ENGINE-side event manager singleton (engine2 keeps its own global to it at
        // engine2+0x9F60C8), NOT the client's 167-slot CGameEventManager composite the old
        // 170 floor was sized from. The game dispatches it POSITIONALLY far higher: measured
        // across ALL call sites loading the object from the global - libclient 844 sites up
        // to [vptr+0x5A8] (slot 181), libengine2 5 sites up to [vptr+0x568] (slot 173).
        // The old 170-slot clone (172 entries = 0x560 bytes) left slots 172-181 reading
        // PAST the buffer into the next pool allocation (the input-hook clone), so
        // engine2's slot-173 call (0x4EDA5E, this = a stack struct) landed on the input
        // clone's slot-1 copy 0x1AD3780 = the CCSGOInput DELETING DESTRUCTOR -> it
        // "deleted" the stack pointer -> tier0 bundled-allocator free crash. 256 covers
        // every observed site with margin; the clone is byte-faithful, so extra copied
        // slots are inert. Do NOT shrink this back to a composite guess: the shared
        // calculator is built from libclient sections and cannot scan this engine2-side
        // vtable (scan = 0), so the floor is the only bound.
        if (gameEventManager && hook.install(vmtLengthCalculator, *reinterpret_cast<std::uintptr_t**>(gameEventManager), 256)) {
            originalFireEventClientSide = hook.hook(9, &GameEventManagerHook_onFireEventClientSide);
        }
    }

    cs2::IGameEventManager2** gameEventManagerGlobal;
    VmtLengthCalculator vmtLengthCalculator;
    VmtSwapper hook;
    cs2::IGameEventManager2::FireEventClientSide* originalFireEventClientSide{ nullptr };
};
