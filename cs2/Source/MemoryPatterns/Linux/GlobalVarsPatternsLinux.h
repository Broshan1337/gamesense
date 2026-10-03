#pragma once

#include <MemoryPatterns/PatternTypes/GlobalVarsPatternTypes.h>
#include <MemorySearch/CodePattern.h>

struct GlobalVarsPatterns {
    [[nodiscard]] static consteval auto addClientPatterns(auto clientPatterns) noexcept
    {
        return clientPatterns
            // 2026-09-23 1.41.8.2: the old comiss-tail anchor is gone; re-anchored on the
            // GlobalVars interpolation reader that loads frametime (0x0C), interval_per_tick
            // (0x1C), +0x20/+0x24/+0x18 back-to-back from the same base register. The layout
            // itself is unchanged (frametime still 0x0C). add(6).read() keeps the same
            // read-at-match+6 contract as the old anchor (the movss disp8 sits at +6).
            // Verified exactly-once.
            .template addPattern<OffsetToFrametime, CodePattern{"F3 0F 10 40 ? F3 0F 10 58 ? F3 0F 10 50 20"}.add(4).add(4).read8()>();
    }
};
