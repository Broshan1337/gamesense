#pragma once

#include <MemoryPatterns/PatternTypes/InfernoPatternTypes.h>
#include <MemorySearch/CodePattern.h>

// Patterns derived offline from the live libclient.so (capstone, verified unique-in-binary against
// full .text) and cross-checked against cs2-dumper: m_nFireEffectTickBegin = 0x18FC,
// m_nFireLifetime = 0x18F0 (2026-08-30 build).
//   * tick begin: the inferno think's guard `cmp qword [rbx+0x8530], 0 / je` right before
//     `mov eax, [rbx+0x18FC] / test eax, eax` (site 0x1B27F30).
//   * lifetime: `movss xmm0, [rbx+0x18F0]` followed by the 70.0f constant write
//     (C7 45 ?? 00 00 8C 42) in the particle-setup branch (site 0x1B28067).
struct InfernoPatterns {
    [[nodiscard]] static consteval auto addClientPatterns(auto clientPatterns) noexcept
    {
        return clientPatterns
            .template addPattern<OffsetToFireEffectTickBegin, CodePattern{"48 83 BB 30 85 00 00 00 0F 84 ? ? ? ? 8B 83 ? ? ? ?"}.add(16).read()>()
            .template addPattern<OffsetToFireLifetime, CodePattern{"F3 0F 10 83 ? ? ? ? C7 45 ? 00 00 8C 42"}.add(4).read()>();
    }
};
