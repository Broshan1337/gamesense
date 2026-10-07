#pragma once

#include <GameClient/EngineCommandExecutor.h>












template <typename HookContext>
class WelcomeSound {
public:
    explicit WelcomeSound(HookContext& hookContext) noexcept
        : hookContext{hookContext}
    {
    }

    void run() const noexcept
    {
        if (played)
            return;

        played = true;
        hookContext.template make<EngineCommandExecutor>().execute(kSound);
    }

private:
    
    
    
    
    
    
    
    static constexpr auto kSound = "play sounds/training/timer_bell";

    
    
    
    
    inline static bool played{false};

    HookContext& hookContext;
};
