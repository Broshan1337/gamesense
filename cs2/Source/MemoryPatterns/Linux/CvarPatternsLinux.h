#pragma once

#include <MemoryPatterns/PatternTypes/CvarPatternTypes.h>
#include <MemorySearch/CodePattern.h>

struct CvarPatterns {
    [[nodiscard]] static consteval auto addClientPatterns(auto clientPatterns) noexcept
    {
        return clientPatterns
            .template addPattern<CvarPointer, CodePattern{"48 89 E5 41 54 53 ? 8D ? ? 48 83 EC ? 48 8D 05 ? ? ? ? 48 8B 38 48 8B 07 FF 50 ? BA"}.add(17).abs()>();
    }

    [[nodiscard]] static consteval auto addTier0Patterns(auto tier0Patterns) noexcept
    {
        return tier0Patterns
            // 2026-09-26 re-derivation (the walk at tier0 0x157A50 = the game's own convar
            // registry iteration): mov rax,[rbx+0x50] = the node-array pointer at CCvar+0x50,
            // then movzx eax,word [rax+r12+0xa] = the per-node next-handle. The old 6-byte
            // pattern matched a decoy "push rbx; push rax" pair (byte-match green, resolved
            // garbage). FieldFieldOffset subtracts offsetof(ConVarList::memory)=8 back to the
            // member base (CCvar+0x48); the head handle sits at CCvar+0x4A (see CUtlLinkedList.h).
            .template addPattern<OffsetToConVarList, CodePattern{"48 8B 43 ? 42 0F B7 44 20 0A"}.add(3).read8()>();
    }
};
