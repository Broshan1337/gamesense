#pragma once

#include <MemoryPatterns/PatternTypes/GameSceneNodePatternTypes.h>
#include <MemorySearch/CodePattern.h>

struct GameSceneNodePatterns {
    [[nodiscard]] static consteval auto addClientPatterns(auto clientPatterns) noexcept
    {
        return clientPatterns
            
            
            
            
            .template addPattern<OffsetToGameSceneNodeOwner, CodePattern{"48 8B 50 ? 48 8B 52 ? F6 42 ? 02 75 ? 48 8B 40 38 48 85 C0 75"}.add(3).add(3).read8()>()
            .template addPattern<OffsetToChildGameSceneNode, CodePattern{"24 ? 48 85 DB 74 ? 48 89 DF E8 ? ? ? ? 48 8B"}.add(1).add(1).read8()>()
            .template addPattern<OffsetToNextSiblingGameSceneNode, CodePattern{"? 48 85 DB 75 ? 49 8B 04 24 4C 89 E7 FF 50"}.read8()>()
            
            
            .template addPattern<GameSceneNodeSetMeshGroupMaskFunction, CodePattern{"48 8B 87 E0 01 00 00 48 85 C0 0F 84 ? ? ? ? 55 48 89 E5 41 57 41 56 41 55 41 54 53 48 89 FB 48 83 EC 08 48 8B 38 48 85 FF"}>()
            
            .template addPattern<GameSceneNodeSetSkeletonFunction, CodePattern{"48 8B 87 E0 01 00 00 48 85 C0 0F 84 ? ? ? ? 55 48 89 E5 41 57 41 56 41 55 41 54 53 48 89 FB 48 81 EC 08 42 00 00"}>();
    }
};
