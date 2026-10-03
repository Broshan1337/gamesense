#pragma once

#include <MemoryPatterns/PatternTypes/InfernoPatternTypes.h>
#include <MemorySearch/CodePattern.h>

// Patterns derived offline from the live libclient.so (capstone, verified unique-in-binary against
// full .text) and cross-checked against cs2-dumper: m_nFireEffectTickBegin = 0x19E4,
// m_nFireLifetime = 0x19D8 (2026-09-23 build, 1.41.8.2; were 0x18FC / 0x18F0 on 2026-08-30).
//   * tick begin: the inferno think's guard `cmp qword [rbx+0x8620], 0 / je` right before
//     `mov eax, [rbx+0x19E4] / test eax, eax` (site 0x1B79D00).
//   * lifetime: `movss xmm0, [rbx+0x19D8]` followed by the 70.0f constant write
//     (C7 45 ?? 00 00 8C 42) in the particle-setup branch (site 0x1B79E5D).
// The offset bytes are load-bearing in both (schema-confirmed); verified exactly-once.
struct InfernoPatterns {
    [[nodiscard]] static consteval auto addClientPatterns(auto clientPatterns) noexcept
    {
        return clientPatterns
            .template addPattern<OffsetToFireEffectTickBegin, CodePattern{"48 83 BB 20 86 00 00 00 0F 84 ? ? ? ? 8B 83 ? ? ? ?"}.add(16).read()>()
            .template addPattern<OffsetToFireLifetime, CodePattern{"F3 0F 10 83 ? ? ? ? C7 45 ? 00 00 8C 42"}.add(4).read()>();
    }
};
