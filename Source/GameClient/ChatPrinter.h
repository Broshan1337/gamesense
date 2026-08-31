#pragma once

#include <CS2/Classes/CHudChatDelegate.h>
#include <MemoryPatterns/PatternTypes/ClientPatternTypes.h>

// Prints a real entry into the in-game chat, using the game's own printer.
//
// Shared rather than duplicated: more than one experiment feature wants to write to chat, and the
// printf-format hazard below is the kind of thing that should be got right in exactly one place.
// See CHudChatDelegate.h for how the delegate global and the print function were reverse-engineered.
//
// Local only. This does NOT send anything to the server - it appends to our own client's chat
// display, and nobody else can see it. Deliberately not `say`/`say_team`, which would be real
// network traffic.
template <typename HookContext>
class ChatPrinter {
public:
    explicit ChatPrinter(HookContext& hookContext) noexcept
        : hookContext{hookContext}
    {
    }

    void print(const char* text) const noexcept
    {
        if (!text)
            return;

        const auto chatPrint = hookContext.patternSearchResults().template get<ChatPrintFunction>();
        if (!chatPrint)
            return;

        const auto delegatePointer = hookContext.patternSearchResults().template get<HudChatDelegatePointer>();
        if (!delegatePointer || !*delegatePointer)
            return;

        // Always through a literal "%s". ChatPrint is printf-style, and this now carries text
        // containing player names, which are attacker-controlled and can absolutely contain a
        // percent sign. Passing that as the format string would read arguments never pushed.
        chatPrint(*delegatePointer, static_cast<unsigned int>(-1), "%s", text);
    }

private:
    HookContext& hookContext;
};
