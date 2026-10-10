#pragma once

#include <cstdint>

namespace cs2
{



























struct PlayerRankingData {
    static constexpr int kFlagsOffset = 0x10;
    static constexpr int kLevelOffset = 0xB4;
    static constexpr int kExperienceOffset = 0xB8;

    
    static constexpr int kRankingSubstructOffset = 0x70;
    static constexpr int kRankingSubstructRankingOffset = 0x3C;
    static constexpr int kRankingSubstructWinsOffset = 0x40;
    static constexpr std::uint32_t kRankingPresentFlag = 0x4;

    
    static constexpr int kCommendsSubstructOffset = 0x78;
    static constexpr int kCommendsSubstructFriendlyOffset = 0x18;
    static constexpr int kCommendsSubstructTeachingOffset = 0x1C;
    static constexpr int kCommendsSubstructLeaderOffset = 0x20;
    static constexpr std::uint32_t kCommendsPresentFlag = 0x8;

    
    
    static constexpr std::uint32_t kLevelPresentFlag = 0x2000;
    static constexpr std::uint32_t kExperiencePresentFlag = 0x4000;
};

// The GC rank cache tree (the display-side source the scoreboard/profile actually read -
// writing only the ranking block + controller fields leaves this stale, which is why the
// premier score / wingman rank never showed). Layout cross-checked against the game's own
// tree getter on Linux (0x1EE5660/0x1F68BB0 era) and matches the working reference Lua:
// 136-byte nodes, left/right int32 child indices (-1 = leaf), ranktype key at +16,
// rating at +84, wins at +88.
struct GcRankCacheTree {
    static constexpr int kNodeStride = 136;
    static constexpr int kNodeLeftOffset = 0;
    static constexpr int kNodeRightOffset = 4;
    static constexpr int kNodeKeyOffset = 16;
    static constexpr int kNodeRatingOffset = 84;
    static constexpr int kNodeWinsOffset = 88;
    static constexpr std::uint32_t kFlagMask = 0x7FFFFFFF;
    static constexpr int kInvalidIndex = -1;
    static constexpr int kMaxWalkSteps = 64;
    static constexpr int kMaxSlots = 64;

    static constexpr int kRankTypeWingman = 7;
    static constexpr int kRankTypePremier = 11;
    static constexpr int kRankTypeCompetitive = 12;
};

}
