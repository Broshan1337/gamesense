#pragma once

#include <CS2/Classes/IGameEventManager2.h>
#include <Features/Sound/SpawnProtectionSoundConfigVariables.h>
#include <GameClient/EngineCommandExecutor.h>
#include <GameClient/GameEvents/GameEventFields.h>
#include <HookContext/HookContextMacros.h>








template <typename HookContext>
class SpawnProtectionSound {
public:
    explicit SpawnProtectionSound(HookContext& hookContext) noexcept
        : hookContext{hookContext}
    {
    }

    void onFireEventClientSide(cs2::IGameEvent* event) const noexcept
    {
        if (!event || !GET_CONFIG_VAR(SpawnProtectionSoundEnabled))
            return;

        if (!game_events::is(event, "round_freeze_end"))
            return;

        const auto now = hookContext.globalVars().curtime();
        if (!now.hasValue())
            return;

        armed = true;
        endTime = now.value() + kProtectionSeconds;
    }

    
    void run() const noexcept
    {
        if (!armed)
            return;

        
        
        if (!GET_CONFIG_VAR(SpawnProtectionSoundEnabled)) {
            armed = false;
            return;
        }

        const auto now = hookContext.globalVars().curtime();
        if (!now.hasValue())
            return;

        
        
        if (now.value() < endTime - kProtectionSeconds) {
            armed = false;
            return;
        }

        if (now.value() < endTime)
            return;

        
        
        armed = false;
        hookContext.template make<EngineCommandExecutor>().execute("play sounds/training/timer_bell");
    }

private:
    
    
    
    static constexpr float kProtectionSeconds = 5.0f;

    
    
    inline static bool armed = false;
    inline static float endTime = 0.0f;

    HookContext& hookContext;
};
