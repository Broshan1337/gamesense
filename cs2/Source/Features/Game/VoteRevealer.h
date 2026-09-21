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

// Reveals who voted what, by printing each vote_cast to our own chat as it arrives.
//
// Despite appearances this is no less local than the rest of this project: vote_cast is an event
// the client is already sent and already processes - the game simply chooses not to surface who
// voted which way. Nothing is requested from the server and nothing is transmitted; the reference
// implementation's party-broadcast path is deliberately not reproduced.
//
// Honest limitation: this can only reveal votes the client actually receives. If the server never
// sends the other team's vote_cast events, no amount of client-side work will show them - what
// gets printed is whatever really arrived.
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
        // Name in default colour, then the vote itself coloured by what it was, so a YES/NO can be
        // picked out of a busy chat at a glance.
        builder.put(cs2::chat_color::kDefault, lookup.nameBySlot(voterSlot), " voted ", voteOptionColor(event), voteOptionName(event), cs2::chat_color::kDefault);

        hookContext.template make<ChatPrinter>().print(builder.cstring());
    }

private:
    // Source's vote options are an enum starting at VOTE_OPTION1 = 0, and for the yes/no votes
    // players actually see (kick, surrender, timeout) option 1 is Yes and option 2 is No. The
    // reference implementation maps it the same way.
    //
    // Unlike that reference, anything beyond the first two is reported by number rather than being
    // lumped in with "NO": some vote types carry more than two options, and silently calling a
    // third option "NO" would be a confident lie. -1 covers the field being absent entirely.
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
