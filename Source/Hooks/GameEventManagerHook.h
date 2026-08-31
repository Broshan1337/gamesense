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
        const auto gameEventManager = getGameEventManager();
        if (gameEventManager && hook.install(vmtLengthCalculator, *reinterpret_cast<std::uintptr_t**>(gameEventManager), 9 + 1)) {
            originalFireEventClientSide = hook.hook(9, &GameEventManagerHook_onFireEventClientSide);
        }
    }

    cs2::IGameEventManager2** gameEventManagerGlobal;
    VmtLengthCalculator vmtLengthCalculator;
    VmtSwapper hook;
    cs2::IGameEventManager2::FireEventClientSide* originalFireEventClientSide{ nullptr };
};
