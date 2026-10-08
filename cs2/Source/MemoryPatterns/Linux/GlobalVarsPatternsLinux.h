#pragma once

#include <MemoryPatterns/PatternTypes/GlobalVarsPatternTypes.h>
#include <MemorySearch/CodePattern.h>

struct GlobalVarsPatterns {
    [[nodiscard]] static consteval auto addClientPatterns(auto clientPatterns) noexcept
    {
        return clientPatterns
            
            
            
            
            
            
            .template addPattern<OffsetToFrametime, CodePattern{"F3 0F 10 40 ? F3 0F 10 58 ? F3 0F 10 50 20"}.add(4).add(4).read8()>();
    }
};
