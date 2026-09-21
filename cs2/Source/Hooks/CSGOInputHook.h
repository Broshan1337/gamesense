#pragma once

#include <cstdint>

#include <CS2/Classes/CCSGOInput.h>
#include <Vmt/VmtLengthCalculator.h>
#include <Vmt/VmtSwapper.h>

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
        // Highest hooked slot = CreateMove at 26 -> the copy must span 27 entries. The plain
        // length scan truncates on this composite vtable (see vmtCopyLength) - slot 26 once
        // lived entirely in out-of-bounds pool memory.
        if (input && hook.install(vmtLengthCalculator, *reinterpret_cast<std::uintptr_t**>(input), cs2::CCSGOInput::kCreateMoveVtableSlot + 1)) {
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
