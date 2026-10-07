#pragma once

#include <cstring>

#include <CS2/Classes/CHudChatDelegate.h>
#include <CS2/Classes/IEngineClient.h>
#include <CS2/Classes/IGameEventManager2.h>
#include <Features/Game/HitLogConfigVariables.h>
#include <GameClient/EngineClientPointer.h>
#include <GameClient/GameEvents/GameEventFields.h>
#include <MemoryPatterns/PatternTypes/ClientPatternTypes.h>
#include <HookContext/HookContextMacros.h>
#include <Utils/RetAddrSpoofer.h>







template <typename HookContext>
class HitLog {
public:
    explicit HitLog(HookContext& hookContext) noexcept
        : hookContext{hookContext}
    {
    }

    void onFireEventClientSide(cs2::IGameEvent* event) const noexcept
    {
        if (!event)
            return;

        if (!GET_CONFIG_VAR(HitLogEnabled))
            return;

        if (!game_events::is(event, "player_hurt"))
            return;

        if (!game_events::localPlayerIsAttacker(hookContext, event))
            return;

        printLine("hit player!");
    }

private:
    
    
    
    
    
    
    
    
    
    
    
    void printLine(const char* text) const noexcept
    {
        printToChat(text);
        printToConsole(text);
    }

    
    
    
    
    
    
    
    
    
    void printToChat(const char* text) const noexcept
    {
        const auto chatPrint = hookContext.patternSearchResults().template get<ChatPrintFunction>();
        if (!chatPrint)
            return;

        const auto delegatePointer = hookContext.patternSearchResults().template get<HudChatDelegatePointer>();
        if (!delegatePointer || !*delegatePointer)
            return;

        
        
        
        chatPrint(*delegatePointer, static_cast<unsigned int>(-1), "%s", text);
    }

    void printToConsole(const char* text) const noexcept
    {
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

        
        
        
        char command[128]{"echo "};
        auto* out = command + 5;
        const auto end = command + sizeof(command) - 1;
        while (*text && out < end)
            *out++ = *text++;
        *out = '\0';

        RetAddrSpoofer::spoof(reinterpret_cast<cs2::IEngineClient::ExecuteClientCommand*>(executeCommand))(engine, 0, command, 1);
    }

    HookContext& hookContext;
};
