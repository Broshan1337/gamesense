#pragma once

#include <MemoryPatterns/PatternTypes/EntityPatternTypes.h>
#include <MemorySearch/CodePattern.h>

struct EntityPatterns {
    [[nodiscard]] static consteval auto addClientPatterns(auto clientPatterns) noexcept
    {
        return clientPatterns
            .template addPattern<OffsetToGameSceneNode, CodePattern{"2C E0 49 8B 85 ? ? ? ?"}.add(5).read()>()
            // 2026-09-23 1.41.8.2: the zero-init site kept `mov dword ptr [rdi+off], 0` but the
            // following lea recompiled (lea rdi,[rbp-0x40] instead of lea rsi) and the health
            // offset moved to 0x4BC (schema-confirmed m_iHealth 1212). The health offset bytes
            // themselves are load-bearing now (BC 04 00 00) - verified exactly-once.
            .template addPattern<OffsetToHealth, CodePattern{"C7 87 ? ? ? ? ? ? ? ? 48 8D 7D"}.add(2).read()>()
            // 2026-09-23 1.41.8.2: same shape, offset moved 0x3E1 -> 0x4C4 (schema m_lifeState 1220).
            // The offset bytes are load-bearing; verified exactly-once.
            .template addPattern<OffsetToLifeState, CodePattern{"0F B6 97 ? ? ? ? 39 F2"}.add(3).read()>()
            .template addPattern<OffsetToTeamNumber, CodePattern{"? ? ? ? 02 48 8D 05 ? ? ? ? 74 ? 48"}.read()>()
            .template addPattern<OffsetToVData, CodePattern{"? ? ? ? 5A 59 48 85 C0 74 ? 4C"}.read()>()
            // 2026-09-23 1.41.8.2: offset moved to 0x4A8 (schema m_pRenderComponent 1192); the
            // offset bytes are load-bearing, verified exactly-once.
            .template addPattern<OffsetToRenderComponent, CodePattern{"49 8B BC 24 ? ? ? ? 48 85 FF 74 ? 8B 47"}.add(4).read()>()
            // 2026-09-23 1.41.8.2: offset moved to 0x698 (schema m_hOwnerEntity 1688) and the
            // site's epilogue recompiled (test/jne/add-rsp/xor tail gone -> test/je). The
            // offset bytes stay load-bearing (the site: mov ebx,<off>; call <owner-of>; test/je);
            // verified exactly-once.
            .template addPattern<OffsetToOwnerEntity, CodePattern{"BB ? ? ? ? E8 ? ? ? ? 48 85 C0 74 32"}.add(1).read()>()
            .template addPattern<GetAbsOriginFunction, CodePattern{"4C 8B ? E8 ? ? ? ? F3 0F 10 05"}.add(4).abs()>();
    }
};
