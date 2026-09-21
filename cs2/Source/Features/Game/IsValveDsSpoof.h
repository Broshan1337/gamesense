#pragma once

#include <Features/Game/ValveDsSpoofConfigVariables.h>
#include <HookContext/HookContextMacros.h>
#include <Utils/StringBuilder.h>
#include <Utils/VerifyConsole.h>

// Forces the client's copy of C_CSGameRules::m_bIsValveDS to false while enabled - the
// isvalveds_check port from FORFUTURETESTS/mytest.
//
// Several client-side restrictions key off this flag (the "Valve official server" checks); the
// reconstruction forced it to 0 every tick from its Hook A body with an idempotent one-branch
// write. Same shape here, gated behind a config switch and rate-limited so a flip is visible in
// exactly one log line. Server-authoritative state elsewhere is untouched - this is a local
// byte in the client's own game rules entity, written through the already-resolved
// GameRulesPointer pattern, at an offset anchored to the cs2-dumper dump of this build (see
// cs2::C_CSGameRules::kIsValveDsOffset).
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
