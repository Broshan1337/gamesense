#pragma once

#include <cstdint>

namespace cs2
{

// The client's own copy of the local player's ranking data - the block the game reads when it
// publishes "game/level" and "game/xppts" into the player's KeyValues.
//
// Found in sub_1ED8280, which loads the whole block once (`lea rbx, unk_4817720`) and then reaches
// every member through it:
//
//   if ((flags & 0x2000) != 0) level = *(uint32*)(base + 0xB4);
//   if ((flags & 0x4000) != 0) xp    = *(uint32*)(base + 0xB8);
//
// Cross-checked against the Windows build, where the same three live at flags+0, flags+0xA4 and
// flags+0xA8 - identical relative layout on both platforms, which is a strong sign these offsets
// are the real structure rather than a coincidence of one build.
struct PlayerRankingData {
    static constexpr int kFlagsOffset = 0x10;
    static constexpr int kLevelOffset = 0xB4;
    static constexpr int kExperienceOffset = 0xB8;

    // The game reports 0 for a value whose flag is clear, no matter what the field holds - so
    // writing a level without setting its flag would do nothing at all.
    static constexpr std::uint32_t kLevelPresentFlag = 0x2000;
    static constexpr std::uint32_t kExperiencePresentFlag = 0x4000;
};

}
