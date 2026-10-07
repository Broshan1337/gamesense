#pragma once

#include <MemoryPatterns/PatternTypes/SchemaSystemPatternTypes.h>
#include <MemorySearch/CodePattern.h>






struct SchemaSystemPatterns {
    [[nodiscard]] static consteval auto addSchemaSystemPatterns(auto schemaSystemPatterns) noexcept
    {
        return schemaSystemPatterns
            
            
            
            
            
            
            
            
            .template addPattern<GlobalTypeScopePointer, CodePattern{"55 48 89 E5 41 57 41 56 49 89 F6 41 55 49 89 FD 41 54 53 48 83 EC 18 48 8D 3D ? ? ? ? 48 89 55 C8 E8 ? ? ? ? 48 85 C0 0F 85 ? ? ? ? 48 8D 3D ? ? ? ? 4C"}.add(26).abs()>()
            
            
            
            
            
            
            
            
            
            
            
            
            
            
            
            
            
            
            
            
            
            
            .template addPattern<PointerToSchemaFindDeclaredClassOrEnum, CodePattern{"55 48 89 E5 41 57 41 56 41 55 49 89 FD 41 54 53 48 83 EC 08 48 85 F6 0F 84 ? ? ? ? 45 31 E4 80 3E 00 48 89 F3 0F 84 ? ? ? ? 48 89 F7 E8 CC D9"}>()
            
            
            
            
            
            
            
            
            
            
            
            
            
            
            
            
            .template addPattern<PointerToSchemaBeginFieldIterator, CodePattern{"48 B8 08 00 00 00 00 00 00 80 55 48 89 E5 41 56 4C 8D 77 18 41 55 49 89 F5 41 54 41 89 D4 53 48 89 FB 48 C7 47 08 00 00 00 00 48 89 47 10 83 E7 07 0F 85 71 01 00 00 66 0F EF C0 4C"}>()
            
            .template addPattern<PointerToSchemaFieldIteratorHasNext, CodePattern{"48 83 BF B0 00 00 00 00 0F 95 C0 C3"}>()
            
            .template addPattern<PointerToSchemaFieldIteratorCurrent, CodePattern{"48 8B 87 B0 00 00 00 48 85 C0 74 04 48 8B 40 08 C3"}>()
            
            .template addPattern<PointerToSchemaFieldIteratorNext, CodePattern{"48 83 BF B0 00 00 00 00 48 89 F8 8B 97 A8 00 00 00 74 3D 85 D2 78 3D 8B"}>()
            
            .template addPattern<PointerToSchemaFieldIteratorOffset, CodePattern{"48 8B 97 B0 00 00 00 8B 87 AC 00 00 00 03 42 10 C3"}>();
    }
};
