#pragma once

#include <MemoryPatterns/PatternTypes/GameSceneNodePatternTypes.h>
#include <MemorySearch/CodePattern.h>

struct GameSceneNodePatterns {
    [[nodiscard]] static consteval auto addClientPatterns(auto clientPatterns) noexcept
    {
        return clientPatterns
            .template addPattern<OffsetToGameSceneNodeOwner, CodePattern{"BD ? ? ? ? 00 0F 85 ? ? ? ? ? 8B ? ? 31 D2"}.add(15).read()>()
            .template addPattern<OffsetToChildGameSceneNode, CodePattern{"24 ? 48 85 DB 74 ? 48 89 DF E8 ? ? ? ? 48 8B"}.add(1).read()>()
            .template addPattern<OffsetToNextSiblingGameSceneNode, CodePattern{"? 48 85 DB 75 ? 49 8B 04 24 4C 89 E7 FF 50"}.read()>()
            // CSkeletonInstance::setMeshGroupMask - prologue ends in `sub $0x8,%rsp` (vs the skeleton
            // setter's unique `sub $0x4208`). See GameSceneNodePatternTypes.h for identification notes.
            .template addPattern<GameSceneNodeSetMeshGroupMaskFunction, CodePattern{"48 8B 87 E0 01 00 00 48 85 C0 0F 84 ? ? ? ? 55 48 89 E5 41 57 41 56 41 55 41 54 53 48 89 FB 48 83 EC 08 48 8B 38 48 85 FF"}>()
            // CSkeletonInstance::setSkeleton - the ONLY function in the binary with a 0x4208-byte stack.
            .template addPattern<GameSceneNodeSetSkeletonFunction, CodePattern{"48 8B 87 E0 01 00 00 48 85 C0 0F 84 ? ? ? ? 55 48 89 E5 41 57 41 56 41 55 41 54 53 48 89 FB 48 81 EC 08 42 00 00"}>();
    }
};
