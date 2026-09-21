#pragma once

#include <CS2/Classes/IEngineClient.h>
#include <GameClient/EngineClientPointer.h>
#include <Utils/RetAddrSpoofer.h>

// Runs a string through the engine's client command buffer, exactly as if it had been typed into
// the console. Shared by everything in this project that needs a console command.
//
// Note the command is QUEUED, not executed inline: ExecuteClientCommand formats the string and
// hands it to the engine's command buffer (a Cbuf_AddText equivalent), which drains on the next
// frame pass. That costs about one frame and is not perceptible - confirmed by decompiling it.
//
// Local only: this drives our own client's console, and nothing here sends anything to a server.
template <typename HookContext>
class EngineCommandExecutor {
public:
    explicit EngineCommandExecutor(HookContext& hookContext) noexcept
        : hookContext{hookContext}
    {
    }

    // The command must be a trusted, compile-time string. It is NOT escaped or quoted, and the
    // buffer treats newlines as command separators, so never build one out of player names, chat
    // text, or anything else off the network.
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

        // Argument shape taken from the working reference caller: (this, 0, command, 1).
        const auto executeCommandFn = reinterpret_cast<cs2::IEngineClient::ExecuteClientCommand*>(executeCommand);
        return RetAddrSpoofer::spoof(executeCommandFn)(engine, 0, command, 1);
    }

private:
    HookContext& hookContext;
};
