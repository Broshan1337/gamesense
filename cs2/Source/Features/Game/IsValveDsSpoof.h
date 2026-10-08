#pragma once

#include <Features/Game/ValveDsSpoofConfigVariables.h>
#include <HookContext/HookContextMacros.h>
#include <Utils/StringBuilder.h>
#include <Utils/VerifyConsole.h>











template <typename HookContext>
class IsValveDsSpoof {
public:
    explicit IsValveDsSpoof(HookContext& hookContext) noexcept
        : hookContext{hookContext}
    {
    }

    void run() const noexcept
    {
        if (!GET_CONFIG_VAR(ValveDsSpoofEnabled))
            return;

        if (hookContext.gameRules().spoofValveDs(false))
            VerifyConsole::write(60.0f, "[valveds]", "m_bIsValveDS forced to false\n");
    }

private:
    HookContext& hookContext;
};
