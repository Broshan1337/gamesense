#pragma once

#include <cstdint>

#include <CS2/Classes/IGameEventManager2.h>
#include <Vmt/VmtLengthCalculator.h>
#include <Vmt/VmtSwapper.h>

bool GameEventManagerHook_onFireEventClientSide(cs2::IGameEventManager2* thisptr, cs2::IGameEvent* event) noexcept;

























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
        
        
        
        
        
        if (gameEventManager && hook.install(vmtLengthCalculator, *reinterpret_cast<std::uintptr_t**>(gameEventManager), 170)) {
            originalFireEventClientSide = hook.hook(9, &GameEventManagerHook_onFireEventClientSide);
        }
    }

    cs2::IGameEventManager2* gameEventManager;
    VmtLengthCalculator vmtLengthCalculator;
    VmtSwapper hook;
    cs2::IGameEventManager2::FireEventClientSide* originalFireEventClientSide{ nullptr };
};
