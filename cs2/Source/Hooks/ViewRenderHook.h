#pragma once

#include <cstdint>

#include <CS2/Classes/CViewRender.h>
#include <Vmt/VmtLengthCalculator.h>
#include <Vmt/VmtSwapper.h>

void ViewRenderHook_onRenderStart(cs2::CViewRender* thisptr) noexcept;

class ViewRenderHook {
public:
    ViewRenderHook(cs2::CViewRender** viewRender, const VmtLengthCalculator& vmtLengthCalculator) noexcept
        : viewRender{viewRender}
        , vmtLengthCalculator{vmtLengthCalculator}
    {
    }

    [[nodiscard]] cs2::CViewRender::OnRenderStart* getOriginalOnRenderStart() const noexcept
    {
        return originalOnRenderStart;
    }

    void uninstall() const noexcept
    {
        if (viewRender && *viewRender)
            hook.uninstall(*reinterpret_cast<std::uintptr_t**>(*viewRender));
    }

    [[nodiscard]] bool isInstalled() const noexcept
    {
        return hook.wasEverInstalled() && viewRender && *viewRender && hook.isInstalled(*reinterpret_cast<std::uintptr_t**>(*viewRender));
    }

    void install() noexcept
    {
        // 09-26 RE-ENABLED: the earlier fail-close (slot 4 = "an offset-to-top header pair") was
        // an artifact of reading UNRELOCATED file bytes of .data.rel.ro (PIE - the runtime values
        // live in R_X86_64_RELATIVE addends, file bytes are stale/zero). Reloc-correct read:
        // CViewRender composite @0x44fdb98 is contiguous code for 439 slots; slot 4 =
        // 0x19d0ca0 = the no-arg render-start called exactly once per frame from the engine's
        // render loop (the one no-arg [vptr+0x20] site). Same slot index as the 09-12
        // derivation. The whole game-thread pipeline (config load/save, loader-unload poll,
        // session tick, HUD/visual feature managers) lives in the OnRenderStart hook body.
        if (viewRender && *viewRender && hook.install(vmtLengthCalculator, *reinterpret_cast<std::uintptr_t**>(*viewRender), 4 + 1)) {
            originalOnRenderStart = hook.hook(4, &ViewRenderHook_onRenderStart);
        }
    }

    cs2::CViewRender** viewRender;
    VmtLengthCalculator vmtLengthCalculator;
    VmtSwapper hook;
    cs2::CViewRender::OnRenderStart* originalOnRenderStart{ nullptr };
};
