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

// Experiment logging: prints a line every time the LOCAL player damages someone. Shares its
// trigger condition with HitSound (same event, same attacker test - see GameEventFields.h), but is
// toggled separately so a log can be collected without a bell ringing, or vice versa.
//
// Intended as the first entry in a "Game" tab of experiment instrumentation, so the output plumbing
// below is deliberately generic - printLine() is the part worth reusing for the next experiment.
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
    // Two independent channels, on purpose.
    //
    // The console one is the RELIABLE record: it goes through the same ExecuteClientCommand path
    // the hit sound already proved works end-to-end, so an experiment never silently loses data.
    // The chat one is the nicer place to read it live, but depends on locating a panel whose
    // layout-file id could not be confirmed offline (see Hud::chatMessageContainer), so it is
    // strictly best-effort and degrades to "console only" rather than failing the whole print.
    //
    // Nothing here touches the network. In particular this deliberately does NOT use the `say`
    // command, which would transmit a real chat message to the server - the line is composed
    // client-side into our own panel and is visible to nobody else.
    void printLine(const char* text) const noexcept
    {
        printToChat(text);
        printToConsole(text);
    }

    // Appends a REAL chat entry, using the game's own printer.
    //
    // An earlier attempt here parented a Panorama label into the chat panel instead. That drew
    // text roughly where chat lives but was not a chat message - it did not scroll, fade, or line
    // up with real entries, because it was simply a floating label on top of them.
    //
    // The delegate is a plain module global on Linux, so unlike the Windows equivalent this needs
    // no FindHudElement("HudChatDelegate") lookup at all. See CHudChatDelegate.h for the RE trail
    // and for why the arguments are (delegate, -1, text).
    void printToChat(const char* text) const noexcept
    {
        const auto chatPrint = hookContext.patternSearchResults().template get<ChatPrintFunction>();
        if (!chatPrint)
            return;

        const auto delegatePointer = hookContext.patternSearchResults().template get<HudChatDelegatePointer>();
        if (!delegatePointer || !*delegatePointer)
            return;

        // "%s" rather than passing text as the format directly. Today text is always a literal
        // from this file, but ChatPrint is printf-style, and the moment anything dynamic reaches
        // it a stray percent sign would read arguments that were never passed.
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

        // `echo` prints its argument to the developer console. The text is a compile-time literal
        // from this file, never user or network input, so there is nothing here that could inject
        // a second command through the buffer's newline separator.
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
