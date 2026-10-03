#pragma once

#include <cstdint>

#include <CS2/Classes/CSource2Client.h>
#include <Vmt/VmtLengthCalculator.h>
#include <Vmt/VmtSwapper.h>
#include <Utils/StatusReport.h>

void Source2ClientHook_onFrameStageNotify(cs2::CSource2Client* thisptr, int frameStage) noexcept;

// CSource2Client is a true process-lifetime singleton (unlike CViewRender, which ViewRenderHook
// tracks via a live CViewRender** since that pointer can be reassigned across level loads) - so
// this stores the resolved object pointer directly rather than a pointer-to-pointer.
class Source2ClientHook {
public:
    Source2ClientHook(cs2::CSource2Client* source2Client, const VmtLengthCalculator& vmtLengthCalculator) noexcept
        : source2Client{source2Client}
        , vmtLengthCalculator{vmtLengthCalculator}
    {
    }

    [[nodiscard]] cs2::CSource2Client::OnFrameStageNotify* getOriginalOnFrameStageNotify() const noexcept
    {
        return originalOnFrameStageNotify;
    }

    void uninstall() const noexcept
    {
        if (source2Client)
            hook.uninstall(*reinterpret_cast<std::uintptr_t**>(source2Client));
    }

    [[nodiscard]] bool isInstalled() const noexcept
    {
        return hook.wasEverInstalled() && source2Client && hook.isInstalled(*reinterpret_cast<std::uintptr_t**>(source2Client));
    }

    void install() noexcept
    {
        // 09-26 RE-ENABLED: reloc-correct verification - CSource2Client vtable @0x44f1c40,
        // slot 36 = 0x192be20 = (this, stage): stages the previous-stage member release
        // ([this+0x460]), stores the stage, calls the GEM slot-86 query, then the STAGE
        // SWITCH (cmp ebx,9 / jump table) = FrameStageNotify's exact shape. Same slot as the
        // 09-12 derivation. (The earlier fail-close was the unrelocated-file-bytes artifact.)
        // minSlots 460 (09-26): the CSource2Client composite measures 449 slots and the game
        // dispatches it POSITIONALLY (the 09-24 network-messages pool-overrun crash class - a
        // 37-slot clone made every dispatch past slot 37 read out of the pool allocation =
        // heap corruption -> the delayed wild-vtable crash in CPanel2D::Initialize).
        if (source2Client && hook.install(vmtLengthCalculator, *reinterpret_cast<std::uintptr_t**>(source2Client), 460)) {
            originalOnFrameStageNotify = hook.hook(36, &Source2ClientHook_onFrameStageNotify);
        }
    }

    cs2::CSource2Client* source2Client;
    VmtLengthCalculator vmtLengthCalculator;
    VmtSwapper hook;
    cs2::CSource2Client::OnFrameStageNotify* originalOnFrameStageNotify{ nullptr };
};
