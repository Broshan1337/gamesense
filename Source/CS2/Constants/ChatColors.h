#pragma once

namespace cs2::chat_color
{

// Source's in-band chat colour codes: a control character in the message text switches the colour
// of everything after it, until the next code. This is the same mechanism the reference
// implementation uses inline (its "\x03name\x01 has \x02..." strings).
//
// Kept as separate constants rather than written inline for a concrete reason: C++ hex escapes are
// GREEDY. "\x0E" immediately followed by a hex digit - "\x0Eabc" - is parsed as one escape with the
// value 0x0EA, not as \x0E followed by 'a'. Ending the literal after the code sidesteps that
// entirely, so a player name beginning with a-f can never corrupt the message.
// kDefault/kRed/kTeam come from the reference implementation; kRed and kGreen are additionally
// confirmed by observation - the vote revealer renders YES green and NO red correctly in game.
constexpr auto kDefault = "\x01";
constexpr auto kRed = "\x02";
constexpr auto kTeam = "\x03";
constexpr auto kGreen = "\x04";

}
