#pragma once

#include <cstdint>

#include <CS2/Classes/CCSGOInput.h>
#include <UI/ImGui/GuiLog.h>
#include <Vmt/VmtLengthCalculator.h>
#include <Vmt/VmtSwapper.h>
#include <Utils/StatusReport.h>

void CSGOInputHook_onCreateMove(cs2::CCSGOInput* thisptr, int slot, cs2::CUserCmd* cmd) noexcept;
std::uint64_t CSGOInputHook_onBuildUserCmd(cs2::CCSGOInput* thisptr, int slot, int frameNumber) noexcept;
std::uint64_t CSGOInputHook_onWriteMoveCrc(cs2::CCSGOInput* thisptr, cs2::CUserCmd* cmd) noexcept;

// Hooks CCSGOInput::CreateMove - the first hook in this project on the INPUT path rather than the
// render or event path.
//
// Like Source2ClientHook this stores the object directly rather than a pointer-to-pointer, and for
// the same reason: CCSGOInput is a process-lifetime singleton. It is stronger than that here, in
// fact - the instance is a static global object constructed in place inside libclient.so, so its
// address is fixed for the life of the process and cannot be reassigned across level loads.
class CSGOInputHook {
public:
    CSGOInputHook(cs2::CCSGOInput* input, const VmtLengthCalculator& vmtLengthCalculator) noexcept
        : input{input}
        , vmtLengthCalculator{vmtLengthCalculator}
    {
    }

    [[nodiscard]] cs2::CCSGOInput::CreateMove* getOriginalCreateMove() const noexcept
    {
        return originalCreateMove;
    }

    [[nodiscard]] cs2::CCSGOInput::BuildUserCmd* getOriginalBuildUserCmd() const noexcept
    {
        return originalBuildUserCmd;
    }

    [[nodiscard]] cs2::CCSGOInput::WriteMoveCrc* getOriginalWriteMoveCrc() const noexcept
    {
        return originalWriteMoveCrc;
    }

    void uninstall() const noexcept
    {
        if (input)
            hook.uninstall(*reinterpret_cast<std::uintptr_t**>(input));
    }

    [[nodiscard]] bool isInstalled() const noexcept
    {
        return hook.wasEverInstalled() && input && hook.isInstalled(*reinterpret_cast<std::uintptr_t**>(input));
    }

    void install() noexcept
    {
        // 09-26 RE-ENABLED (post root-cause): the input hook was exonerated twice - the
        // 23:46 crash fired with this hook OFF (that one = the SILENT OFFSET-PATTERN
        // BREAKAGE class, PanelStyleOffset et al. resolving garbage displacements; fixed
        // by the 2026-09-26 full value audit), and the 5x tier0-free "wild D2" crashes
        // (22:15/22:54/23:19/01:00/01:12) were the GEM HOOK CLONE OVERRUN, not this hook:
        // engine2's slot-173 dispatch on the event manager read past the GEM clone's
        // 170-slot floor into THIS hook's pool-adjacent clone and executed its slot-1 copy
        // (0x1AD3780 = the deleting destructor) with a stack `this`. This hook's surface
        // verified independently on this build: the anchor global holds the in-place
        // object sane (vptr = 0x4518480, ctor caller 0x1B0BBD0), slots 26/6/7 still at
        // their vtable positions, the CUserCmd write anchors byte-identical to the game's
        // own slot-6 code, composite = 42 slots << the 128 clone floor.
        // VoiceTap stays disabled per the standing rule.
        if (input && hook.install(vmtLengthCalculator, *reinterpret_cast<std::uintptr_t**>(input), 128)) {
            originalCreateMove = hook.hook(cs2::CCSGOInput::kCreateMoveVtableSlot, &CSGOInputHook_onCreateMove);
            // Both slots go through the same VmtSwapper - one replacement vtable, two entries
            // swapped in it - so uninstall() still restores everything in one step.
            originalBuildUserCmd = hook.hook(cs2::CCSGOInput::kBuildUserCmdVtableSlot, &CSGOInputHook_onBuildUserCmd);
            originalWriteMoveCrc = hook.hook(cs2::CCSGOInput::kWriteMoveCrcVtableSlot, &CSGOInputHook_onWriteMoveCrc);
        }
    }

    cs2::CCSGOInput* input;
    VmtLengthCalculator vmtLengthCalculator;
    VmtSwapper hook;
    cs2::CCSGOInput::CreateMove* originalCreateMove{nullptr};
    cs2::CCSGOInput::BuildUserCmd* originalBuildUserCmd{nullptr};
    cs2::CCSGOInput::WriteMoveCrc* originalWriteMoveCrc{nullptr};
};
