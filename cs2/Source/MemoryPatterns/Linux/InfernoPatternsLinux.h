#pragma once

#include <MemoryPatterns/PatternTypes/InfernoPatternTypes.h>
#include <MemorySearch/CodePattern.h>









struct InfernoPatterns {
    [[nodiscard]] static consteval auto addClientPatterns(auto clientPatterns) noexcept
    {
        return clientPatterns
            .template addPattern<OffsetToFireEffectTickBegin, CodePattern{"48 83 BB 20 86 00 00 00 0F 84 ? ? ? ? 8B 83 ? ? ? ?"}.add(16).read()>()
            .template addPattern<OffsetToFireLifetime, CodePattern{"F3 0F 10 83 ? ? ? ? C7 45 ? 00 00 8C 42"}.add(4).read()>();
    }
};
