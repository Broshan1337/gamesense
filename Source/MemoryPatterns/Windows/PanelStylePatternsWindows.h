#pragma once

#include <MemoryPatterns/PatternTypes/PanelStylePatternTypes.h>
#include <MemorySearch/CodePattern.h>

struct PanelStylePatterns {
    [[nodiscard]] static consteval auto addPanoramaPatterns(auto panoramaPatterns) noexcept
    {
        return panoramaPatterns
            .template addPattern<SetPanelStylePropertyFunctionPointer, CodePattern{"E8 ? ? ? ? 48 8D 05 ? ? ? ? 48 89 45 ? EB ? 0F"}.add(1).abs()>()
            .template addPattern<PointerToStylePropertySymbols, CodePattern{"FF ? ? ? ? 4C 89 25 ? ? ? ?"}.add(8).abs()>();
    }
};
