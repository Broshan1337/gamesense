#pragma once

#include <CS2/Classes/IEngineClient.h>
#include <GameClient/EngineClientPointer.h>
#include <Utils/RetAddrSpoofer.h>









template <typename HookContext>
class EngineCommandExecutor {
public:
    explicit EngineCommandExecutor(HookContext& hookContext) noexcept
        : hookContext{hookContext}
    {
    }

    
    
    
    void execute(const char* command) const noexcept
    {
        if (!command)
            return;

        const EngineClientPointer engineClient{};
        if (!engineClient)
            return;

        const auto engine = engineClient.get();
        const auto vtable = *reinterpret_cast<void* const* const*>(engine);
        if (!vtable)
            return;

        const auto executeCommand = vtable[cs2::IEngineClient::kExecuteClientCommandVtableSlot];
        if (!executeCommand)
            return;

        
        const auto executeCommandFn = reinterpret_cast<cs2::IEngineClient::ExecuteClientCommand*>(executeCommand);
        return RetAddrSpoofer::spoof(executeCommandFn)(engine, 0, command, 1);
    }

private:
    HookContext& hookContext;
};
