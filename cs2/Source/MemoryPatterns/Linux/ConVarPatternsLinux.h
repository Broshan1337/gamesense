#pragma once

#include <MemoryPatterns/PatternTypes/ConVarPatternTypes.h>
#include <MemorySearch/CodePattern.h>

struct ConVarPatterns {
    [[nodiscard]] static consteval auto addTier0Patterns(auto tier0Patterns) noexcept
    {
        return tier0Patterns
            // OffsetToConVarValue = the value-union slot: the registration path computes the
            // value address as alloc_result + 0x58 (lea rax,[rax+0x58] at 0x164287, disp8 →
            // read8). 2026-09-26: OffsetToConVarValueType's pattern was REMOVED - its site
            // drifted into mid-instruction garbage (byte-match green, resolved 0x4f63bdd =
            // wild reads in the CvarSystem type checks). This build has NO [conVar+0x28]
            // anchor in tier0 (the type dispatch is virtual); CvarSystem uses the
            // friend-verified layout constant (type u32 @0x28, see CvarSystem.h) instead.
            .template addPattern<OffsetToConVarValue, CodePattern{"49 89 ? 48 8D 40 ? 48 89 C7"}.add(6).read8()>();
    }
};
