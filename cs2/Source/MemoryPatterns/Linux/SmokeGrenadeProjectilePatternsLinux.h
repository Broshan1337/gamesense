#pragma once

#include <MemoryPatterns/PatternTypes/SmokeGrenadeProjectilePatternTypes.h>
#include <MemorySearch/CodePattern.h>

struct SmokeGrenadeProjectilePatterns {
    [[nodiscard]] static consteval auto addClientPatterns(auto clientPatterns) noexcept
    {
        return clientPatterns
            .template addPattern<OffsetToDidSmokeEffect, CodePattern{"85 F6 75 ? 80 BF ? ? ? ?"}.add(6).read()>()
            // `mov esi, [rdi+<disp32>] / test esi, esi` at the top of the smoke-activation
            // helper - the didSmokeEffect compare right after pins the same function. The
            // disp32 is wildcarded and .read() takes it live, so the offset tracks recompiles:
            // it was 0x1200 on 1.41.8.2, and 0x12E8 (with m_bDidSmokeEffect at 0x12EC) since
            // the 2026-09-26 5GB build. (The original comment here quoted the 0x1200-era
            // address - one build stale, runtime values were never affected.)
            .template addPattern<OffsetToSmokeEffectTickBegin, CodePattern{"8B B7 ? ? ? ? 85 F6 75 ? 80 BF ? ? ? ? ?"}.add(2).read()>();
    }
};
