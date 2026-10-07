#pragma once

#include <cstdint>

#include <Features/Combat/AttackCommand.h>
#include <Features/Combat/Triggerbot/Triggerbot.h>
#include <Features/Game/PanicKeyConfigVariables.h>
#include <GameClient/Bind.h>
#include <HookContext/HookContextMacros.h>
#include <Utils/VerifyConsole.h>













template <typename HookContext>
class PanicKey {
public:
    explicit PanicKey(HookContext& hookContext) noexcept
        : hookContext{hookContext}
    {
    }

    
    void run() const noexcept
    {
        const int bindValue = GET_CONFIG_VAR(panic_vars::Bind);
        if (bindValue <= Bind::kOff || bindValue > Bind::kLast) {
            
            
            
            
            
            
            if (panicActive) {
                panicActive = false;
                VerifyConsole::write(0.0f, "panic", "bind removed - combat features re-enabled");
            }
            return;
        }

        const bool down = Bind::isDown(bindValue);

        
        
        if (down && !lastKeyWasDown) {
            panicActive = !panicActive;
            if (panicActive)
                engage();
            VerifyConsole::write(0.0f, "panic", "%s - combat features %s",
                panicActive ? "ENGAGED" : "released",
                panicActive ? "disabled" : "enabled");
        }
        lastKeyWasDown = down;
    }

    [[nodiscard]] static bool isActive() noexcept
    {
        return panicActive;
    }

private:
    
    
    
    void engage() const noexcept
    {
        hookContext.template make<Triggerbot>().disarmStatics();
        hookContext.template make<AttackCommand>().reset();
    }

    inline static bool panicActive{false};
    inline static bool lastKeyWasDown{false};

    HookContext& hookContext;
};
