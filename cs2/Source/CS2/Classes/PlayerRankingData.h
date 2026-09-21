#pragma once

#include <cstdint>

namespace cs2
{

// The client's own copy of the local player's ranking data - the block the game reads when it
// publishes "game/level", "game/xppts", "game/ranking", "game/wins", "game/commends" etc. into
// the member KeyValues tree ("members/machine0/player0") that the profile card / lobby UI reads.
//
// The publisher (the function holding the PlayerRankingDataPointer pattern site) reads the block
// like this, every field behind a present-flag bit in the flags dword at +0x10:
//
//   if (flags & 0x2000) level    = *(uint32*)(base + 0xB4);   // game/level
//   if (flags & 0x4000) xp       = *(uint32*)(base + 0xB8);   // game/xppts
//   if (flags & 0x0004) sub      = *(void**)(base + 0x70);    // ranking substruct (GC alloc);
//                                    ranking = *(uint32*)(sub + 0x3C);   // game/ranking
//                                    wins    = *(uint32*)(sub + 0x40);   // game/wins
//                                    wstats  = *(void**)(sub + 0x50);    // game/wstats
//                                    (the publisher falls back to a static default when sub is
//                                     null, but OTHER consumers deref it unguarded - SPOOF RULE:
//                                     only write the flag/ints when the game's own sub exists)
//   if (flags & 0x0008) comm     = *(void**)(base + 0x78);    // commends substruct
//                                    "[f%d][t%d][l%d]" from ints at +0x18 (friendly),
//                                    +0x1C (teaching), +0x20 (leader) -> game/commends
//                                    (SPOOF RULE: same - only the game's own sub, never a
//                                     fabricated struct; the full layout is unknown and the
//                                     2026-09-19 death crash came from exactly that)
//
// The same function also publishes game/prime, game/xptrail, game/teamcolor and the five
// skill-group entries game/v0..v4 (static array, stride 0x108, at its own global - not part of
// this block).
struct PlayerRankingData {
    static constexpr int kFlagsOffset = 0x10;
    static constexpr int kLevelOffset = 0xB4;
    static constexpr int kExperienceOffset = 0xB8;

    // Ranking substruct (competitive / premier rating + wins), gated by its own flag bit.
    static constexpr int kRankingSubstructOffset = 0x70;
    static constexpr int kRankingSubstructRankingOffset = 0x3C;
    static constexpr int kRankingSubstructWinsOffset = 0x40;
    static constexpr std::uint32_t kRankingPresentFlag = 0x4;

    // Commends substruct (friendly / teaching / leader counters), gated by its own flag bit.
    static constexpr int kCommendsSubstructOffset = 0x78;
    static constexpr int kCommendsSubstructFriendlyOffset = 0x18;
    static constexpr int kCommendsSubstructTeachingOffset = 0x1C;
    static constexpr int kCommendsSubstructLeaderOffset = 0x20;
    static constexpr std::uint32_t kCommendsPresentFlag = 0x8;

    // The game reports 0 for a value whose flag is clear, no matter what the field holds - so
    // writing a level without setting its flag would do nothing at all.
    static constexpr std::uint32_t kLevelPresentFlag = 0x2000;
    static constexpr std::uint32_t kExperiencePresentFlag = 0x4000;
};

}
