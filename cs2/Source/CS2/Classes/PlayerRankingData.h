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

}
