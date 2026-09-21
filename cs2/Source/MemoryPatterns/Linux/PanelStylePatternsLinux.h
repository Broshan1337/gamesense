#pragma once

#include <MemoryPatterns/PatternTypes/PanelStylePatternTypes.h>
#include <MemorySearch/CodePattern.h>

struct PanelStylePatterns {
    [[nodiscard]] static consteval auto addPanoramaPatterns(auto panoramaPatterns) noexcept
    {
        return panoramaPatterns
            .template addPattern<SetPanelStylePropertyFunctionPointer, CodePattern{"DF 31 D2 E8 ? ? ? ? 48 8B"}.add(4).abs()>()
            .template addPattern<PointerToStylePropertySymbols, CodePattern{"4C 8D 3D ? ? ? ? 44 0F"}.add(3).abs()>();
    }
};
