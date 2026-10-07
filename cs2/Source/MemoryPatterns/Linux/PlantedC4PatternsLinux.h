#pragma once

#include <MemoryPatterns/PatternTypes/PlantedC4PatternTypes.h>
#include <MemorySearch/CodePattern.h>

struct PlantedC4Patterns {
    [[nodiscard]] static consteval auto addClientPatterns(auto clientPatterns) noexcept
    {
        return clientPatterns
            
            
            
            
            .template addPattern<PlantedC4sPointer, CodePattern{"80 BF BF 08 00 00 00 0F 84 ? ? ? ? 48 8B 05 ? ? ? ? 48 85 C0 0F 84"}.add(16).abs()>()
            
            
            
            .template addPattern<BombSiteOffset, CodePattern{"0F 86 ? ? ? ? 41 8B 86 ? ? ? ? 41 B9"}.add(9).read()>()
            
            
            
            
            .template addPattern<BombTickingOffset, CodePattern{"80 BB ? ? ? ? 00 74 ? C6 83 ? ? ? ? 00 80 BB ? ? ? ? 00 74 07 C6 83 ? ? ? ? 00 80 BB 64 12 00 00 00"}.add(18).read()>()
            
            
            
            .template addPattern<BombBlowTimeOffset, CodePattern{"F3 0F 10 8B ? ? ? ? 0F 2F C8 0F 8A"}.add(4).read()>()
            
            
            
            .template addPattern<BombDefuserOffset, CodePattern{"48 8D BB ? ? ? ? E8 ? ? ? ? 48 85 C0 0F 84 03 01 00 00 4C 8B"}.add(3).read()>()
            .template addPattern<BombDefuseEndTimeOffset, CodePattern{"74 ? F3 0F 10 80 ? ? ? ? 4C"}.add(6).read()>();
    }
};
