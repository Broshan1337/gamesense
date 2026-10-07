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

        // Highest hooked slot = GetViewmodelFov (WIN64_LINUX(27, 29)) -> the copy must span 30
        // entries. install() returns false when the swapper is already installed - then the
        // originals are already captured and hooking again would be a no-op.
        //
        // 2026-10-06 slot fix (Linux 28 -> 29): slot 28 on the current builds is
        // void(this, StatsStruct*) - it writes a byte + five floats through its SECOND argument
        // ([arg2+0x4D8..0x4EC]), it is not a getter. The real GetViewmodelFov is slot 29:
        // float(this) that reads the viewmodel_fov convar wrapper (the static the
        // viewmodel_fov registration function fills: [+8] -> convar object, value at +0x58 -
        // the same ConVar layout as dwSensitivity_sensitivity), RTTI-checks the active weapon
        // and falls back to hardcoded 20.0/54.0. Verified on 1.41.8.8 (vtable 0x4516028:
        // slot 28 = 0x1B1EB50 filler, slot 29 = 0x1ACE060 fov getter) and re-verified on
        // 1.41.8.9 (slot 28 = 0x1B1F150 filler, slot 29 = 0x1ACE660 fov getter reading the
        // wrapper at its new address 0x49435E0). Hooking 28 was benign (the hook forwards
        // rsi untouched) but the FOV feature silently did nothing.
        if (hook().install(hookContext.hooks().clientVmtLengthCalculator, *vmt, WIN64_LINUX(27, 29) + 1)) {
            hookContext.hooks().originalGetViewmodelFov = hook().hook(WIN64_LINUX(27, 29), &ClientModeHook_getViewmodelFov);
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
