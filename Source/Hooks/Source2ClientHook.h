#pragma once

#include <cstdint>

#include <CS2/Classes/CSource2Client.h>
#include <Vmt/VmtLengthCalculator.h>
#include <Vmt/VmtSwapper.h>

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
        if (source2Client && hook.install(vmtLengthCalculator, *reinterpret_cast<std::uintptr_t**>(source2Client), 36 + 1)) {
            originalOnFrameStageNotify = hook.hook(36, &Source2ClientHook_onFrameStageNotify);
        }
    }

    cs2::CSource2Client* source2Client;
    VmtLengthCalculator vmtLengthCalculator;
    VmtSwapper hook;
    cs2::CSource2Client::OnFrameStageNotify* originalOnFrameStageNotify{ nullptr };
};
