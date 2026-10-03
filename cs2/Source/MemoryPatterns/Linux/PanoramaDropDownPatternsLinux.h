#pragma once

#include <MemoryPatterns/PatternTypes/PanoramaDropDownPatternTypes.h>
#include <MemorySearch/CodePattern.h>

struct PanoramaDropDownPatterns {
    [[nodiscard]] static consteval auto addClientPatterns(auto clientPatterns) noexcept
    {
        return clientPatterns
            // 2026-09-23 1.41.8.2: the trailing `test eax,eax` recompiled into an immediate
            // byte-store `mov byte [rdi+0x28], 1`; tail extended to keep exactly-one match.
            .template addPattern<SetSelectedIndexFunctionPointer, CodePattern{"55 48 89 E5 41 56 41 55 41 89 F5 41 54 53 48 89 FB 48 83 EC ? 8B 47 20 C6 47 28 01"}>();
    }
};
