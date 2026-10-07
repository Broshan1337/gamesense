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
        
        
        
        
        
        
        
        
        
        
        
        
        
        if (input && hook.install(vmtLengthCalculator, *reinterpret_cast<std::uintptr_t**>(input), 128)) {
            originalCreateMove = hook.hook(cs2::CCSGOInput::kCreateMoveVtableSlot, &CSGOInputHook_onCreateMove);
            
            
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
