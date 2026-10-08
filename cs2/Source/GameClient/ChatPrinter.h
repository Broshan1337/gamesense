#pragma once

#include <CS2/Classes/CHudChatDelegate.h>
#include <MemoryPatterns/PatternTypes/ClientPatternTypes.h>










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

        
        
        
        chatPrint(*delegatePointer, static_cast<unsigned int>(-1), "%s", text);
    }

private:
    HookContext& hookContext;
};
