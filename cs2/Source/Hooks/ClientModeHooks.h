#pragma once

#include <cstdint>

#include <CS2/Classes/ClientModeCSNormal.h>
#include <CS2/Classes/ViewSetup.h>
#include <MemoryPatterns/PatternTypes/ClientPatternTypes.h>
#include <Vmt/VmtLengthCalculator.h>
#include <Vmt/VmtSwapper.h>

float ClientModeHook_getViewmodelFov(cs2::ClientModeCSNormal* thisptr) noexcept;
void ClientModeHook_onOverrideView(cs2::ClientModeCSNormal* thisptr, ViewSetup* viewSetup) noexcept;

// Both ClientModeCSNormal slots ride the same VmtSwapper (one replacement vtable, two entries
// swapped). OverrideView (WIN64_LINUX(15, 16)) is hooked eagerly at finishInit because two
// ported features (third person, view punch removal) modify the view setup after the original;
// GetViewmodelFov stays gated by its config vars inside the hook body, so the lazy
// install-on-toggle dance the viewmodel mod used is no longer needed.
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

        // Highest hooked slot = GetViewmodelFov (WIN64_LINUX(27, 28)) -> the copy must span 29
        // entries. install() returns false when the swapper is already installed - then the
        // originals are already captured and hooking again would be a no-op.
        if (hook().install(hookContext.hooks().clientVmtLengthCalculator, *vmt, WIN64_LINUX(27, 28) + 1)) {
            hookContext.hooks().originalGetViewmodelFov = hook().hook(WIN64_LINUX(27, 28), &ClientModeHook_getViewmodelFov);
            hookContext.hooks().originalOverrideView = hook().hook(WIN64_LINUX(15, 16), &ClientModeHook_onOverrideView);
        }
    }

    // Kept for the ViewmodelMod config change handler; a no-op once the eager install ran.
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
