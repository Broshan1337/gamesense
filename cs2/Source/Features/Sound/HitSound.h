#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>

#include <CS2/Classes/IEngineClient.h>
#include <CS2/Classes/IGameEventManager2.h>
#include <Features/Sound/HitSoundConfigVariables.h>
#include <GameClient/EngineCommandExecutor.h>
#include <GameClient/GameEvents/GameEventFields.h>
#include <HookContext/HookContextMacros.h>






template <typename HookContext>
class HitSound {
public:
    explicit HitSound(HookContext& hookContext) noexcept
        : hookContext{hookContext}
    {
    }

    void onFireEventClientSide(cs2::IGameEvent* event) const noexcept
    {
        if (!event)
            return;

        if (!GET_CONFIG_VAR(HitSoundEnabled))
            return;

        if (!game_events::is(event, "player_hurt"))
            return;

        if (!game_events::localPlayerIsAttacker(hookContext, event))
            return;

        play();
    }

private:
    
    
    
    
    
    
    
    
    
    
    
    
    void play() const noexcept
    {
        hookContext.template make<EngineCommandExecutor>().execute("play sounds/training/timer_bell");
    }

    HookContext& hookContext;
};
