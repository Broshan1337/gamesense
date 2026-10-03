#pragma once

#include <MemoryPatterns/PatternTypes/GlowSceneObjectPatternTypes.h>
#include <MemorySearch/CodePattern.h>

struct GlowSceneObjectPatterns {
    [[nodiscard]] static consteval auto addClientPatterns(auto clientPatterns) noexcept
    {
        return clientPatterns
            .template addPattern<OffsetToGlowSceneObjectEntity, CodePattern{"85 ? ? ? ? 4C 8B A6 ? ? ? ? 48 89"}.add(8).read()>()
            // 2026-09-23 1.41.8.2: the site recompiled - the two `89` stores after the vcall
            // became `48 89 03` + `4C 89 B0 <disp>` (the .read() disp32), and the unique
            // continuation `48 8B 33 49 8B 46 70` survives at THIS site only (the stack-
            // relative twin's continuation differs). Verified exactly-once.
            .template addPattern<OffsetToGlowSceneObjectAttachedSceneObject, CodePattern{"00 48 8B 07 FF 50 ? 48 89 ? 4C 89 B0 ? ? ? ? 48 8B 33 49 8B 46 70"}.add(13).read()>();
    }
};
