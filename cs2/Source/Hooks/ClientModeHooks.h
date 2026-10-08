#pragma once

#include <cstdint>

#include <CS2/Classes/ClientModeCSNormal.h>
#include <CS2/Classes/ViewSetup.h>
#include <MemoryPatterns/PatternTypes/ClientPatternTypes.h>
#include <Vmt/VmtLengthCalculator.h>
#include <Vmt/VmtSwapper.h>

float ClientModeHook_getViewmodelFov(cs2::ClientModeCSNormal* thisptr) noexcept;
void ClientModeHook_onOverrideView(cs2::ClientModeCSNormal* thisptr, ViewSetup* viewSetup) noexcept;






template <typename HookContext>
class ClientModeHooks {
public:
    explicit ClientModeHooks(HookContext& hookContext) noexcept
        : hookContext{hookContext}
    {
    }

    [[nodiscard]] cs2::ClientModeCSNormal::GetViewmodelFov* originalGetViewmodelFov() const noexcept
    {
        return hookContext.hooks().originalGetViewmodelFov;
    }

    void hookClientMode() const noexcept
    {
        const auto vmt = clientModeInstanceVmtPointer();
        if (!vmt)
            return;

        
        
        
        
        
        
        
        
        
        
        
        
        
        
        
        if (hook().install(hookContext.hooks().clientVmtLengthCalculator, *vmt, WIN64_LINUX(27, 29) + 1)) {
            hookContext.hooks().originalGetViewmodelFov = hook().hook(WIN64_LINUX(27, 29), &ClientModeHook_getViewmodelFov);
            hookContext.hooks().originalOverrideView = hook().hook(WIN64_LINUX(15, 16), &ClientModeHook_onOverrideView);
        }
    }

    
    void hookGetViewmodelFov() const noexcept
    {
        hookClientMode();
    }

    void restoreClientModeHooks() const noexcept
    {
        if (!hook().wasEverInstalled())
            return;

        if (const auto vmt = clientModeInstanceVmtPointer())
            hook().uninstall(*vmt);
    }

private:
    [[nodiscard]] std::uintptr_t** clientModeInstanceVmtPointer() const
    {
        if (const auto clientMode = hookContext.patternSearchResults().template get<PointerToClientMode>())
            return reinterpret_cast<std::uintptr_t**>(clientMode);
        return nullptr;
    }

    [[nodiscard]] auto& hook() const
    {
        return hookContext.hooks().clientModeVmtSwapper;
    }

    HookContext& hookContext;
};
