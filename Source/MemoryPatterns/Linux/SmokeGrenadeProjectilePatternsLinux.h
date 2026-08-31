#pragma once

#include <MemoryPatterns/PatternTypes/SmokeGrenadeProjectilePatternTypes.h>
#include <MemorySearch/CodePattern.h>

struct SmokeGrenadeProjectilePatterns {
    [[nodiscard]] static consteval auto addClientPatterns(auto clientPatterns) noexcept
    {
        return clientPatterns
            .template addPattern<OffsetToDidSmokeEffect, CodePattern{"85 F6 75 ? 80 BF ? ? ? ?"}.add(6).read()>()
            // `mov esi, [rdi+0x1200] / test esi, esi` at the top of the smoke-activation helper -
            // the didSmokeEffect compare right after pins the same function (site 0x1432B0C).
            .template addPattern<OffsetToSmokeEffectTickBegin, CodePattern{"8B B7 ? ? ? ? 85 F6 75 ? 80 BF ? ? ? ? ?"}.add(2).read()>();
    }
};
