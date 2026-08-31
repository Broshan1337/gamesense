#pragma once

namespace cs2
{

// The object behind the in-game chat's message list. Opaque - we only ever hand the pointer back
// to the game's own print function, never read its fields.
//
// Constructed by sub_2001DE0 in libclient.so (a 56-byte object, vtable off_442D830), which stores
// itself into a dedicated module global. On Windows this object is reached with
// FindHudElement("HudChatDelegate"); on Linux that indirection is unnecessary because the global
// is right there - see HudChatDelegatePointer in ClientPatternTypes.h.
//
// It is also reachable as HUD+584 off the HUD singleton (qword_487C8A0, set in the HUD constructor
// sub_1C7E160), which is worth knowing as a fallback if the dedicated global ever disappears.
struct CHudChatDelegate;

// The game's real chat printer: appends an actual chat entry, as opposed to parenting a Panorama
// label over the chat area (which is what an earlier attempt here did - it drew text in roughly
// the right place but was not a chat message and behaved like none).
//
// Reverse-engineered on Linux as sub_1FFDF30. The argument shape was not guessed: sub_1A7A9D0 -
// the game printing its own comms-ban notice - fetches the delegate, localizes
// "#GameUI_VoiceAbusePenaltyNag", and calls this with exactly (delegate, -1, text). That matches
// the known-working Windows caller's (delegate, -1, text) too.
//
// The -1 is the filter/target argument; it means "no specific player".
//
// WARNING: this is printf-style. The third argument is a FORMAT STRING, so anything that could
// contain a percent sign - player names, chat text, anything off the network - must be passed as
// an argument to a literal "%s" and never as the format itself.
using ChatPrint = void*(CHudChatDelegate* thisptr, unsigned int filter, const char* format, ...);

}
