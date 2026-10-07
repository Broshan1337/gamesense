#pragma once

#include <cstdint>

#include <CS2/Classes/IGameEventManager2.h>
#include <CS2/Constants/ChatColors.h>
#include <Features/Game/VoteRevealerConfigVariables.h>
#include <GameClient/ChatPrinter.h>
#include <GameClient/GameEvents/GameEventFields.h>
#include <GameClient/PlayerSlotLookup.h>
#include <HookContext/HookContextMacros.h>
#include <Utils/StringBuilder.h>











template <typename HookContext>
class VoteRevealer {
public:
    explicit VoteRevealer(HookContext& hookContext) noexcept
        : hookContext{hookContext}
    {
    }

    void onFireEventClientSide(cs2::IGameEvent* event) const noexcept
    {
        if (!event || !GET_CONFIG_VAR(VoteRevealerEnabled))
            return;

        if (!game_events::is(event, "vote_cast"))
            return;

        auto&& lookup = hookContext.template make<PlayerSlotLookup>();

        const auto voterSlot = game_events::entityForKey(event, "userid");
        if (!lookup.isValidSlot(voterSlot))
            return;

        StringBuilderStorage<192> storage;
        auto builder = storage.builder();
        
        
        builder.put(cs2::chat_color::kDefault, lookup.nameBySlot(voterSlot), " voted ", voteOptionColor(event), voteOptionName(event), cs2::chat_color::kDefault);

        hookContext.template make<ChatPrinter>().print(builder.cstring());
    }

private:
    
    
    
    
    
    
    
    [[nodiscard]] static const char* voteOptionColor(cs2::IGameEvent* event) noexcept
    {
        switch (game_events::intForKey(event, "vote_option", -1)) {
        case 0: return cs2::chat_color::kGreen;
        case 1: return cs2::chat_color::kRed;
        default: return cs2::chat_color::kDefault;
        }
    }

    [[nodiscard]] static const char* voteOptionName(cs2::IGameEvent* event) noexcept
    {
        switch (game_events::intForKey(event, "vote_option", -1)) {
        case 0: return "YES";
        case 1: return "NO";
        case -1: return "(no option)";
        default: return "another option";
        }
    }

    HookContext& hookContext;
};
