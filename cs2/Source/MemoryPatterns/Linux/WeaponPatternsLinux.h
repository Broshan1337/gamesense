#pragma once

#include <MemoryPatterns/PatternTypes/WeaponPatternTypes.h>
#include <MemorySearch/CodePattern.h>

struct WeaponPatterns {
    [[nodiscard]] static consteval auto addClientPatterns(auto clientPatterns) noexcept
    {
        return clientPatterns
            .template addPattern<OffsetToClipAmmo, CodePattern{"74 ? 8B 87 ? ? ? ? C3"}.add(4).read()>()
            .template addPattern<OffsetToWeaponSceneObjectUpdaterHandle, CodePattern{"48 89 83 ? ? ? ? BE ? ? ? ? 48 89 DF"}.add(3).read()>()
            
            
            
            .template addPattern<PointerToGetInaccuracyFunction, CodePattern{"55 48 89 E5 41 57 41 56 41 55 49 89 D5 41 54 49 89 F4 53 48 89 FB 48 83 EC 48 E8"}>()
            
            
            
            
            // GetSpread reads m_flSpread[weaponMode] from weapon VData. The former
            // item-definition switch was a void weapon action, not a float getter.
            .template addPattern<PointerToGetSpreadFunction, CodePattern{"48 63 87 90 28 00 00 48 8B 97 F8 04 00 00 83 F8 01 76 0D F3 0F 10 82 50 07 00 00"}>()
            
            
            
            
            
            
            
            .template addPattern<PointerToSpreadSeedFunction, CodePattern{"55 89 D0 48 89 E5 53 48 81 EC D8 00 00 00 F3 0F 10 06 0F 2F 05 ? ? ? ? F3 0F 10 1D ? ? ? ? 72 ? 0F 2F D8"}>()
            
            
            
            
            
            
            
            
            
            
            .template addPattern<PointerToCalculateSpreadFunction, CodePattern{"55 48 89 E5 41 57 49 89 D7 41 56 41 55 4D 89 C5 41 54 49 89 CC 53 89 FB 48 81 EC 58 04 00 00 8B 45 40"}>()
            
            
            
            
            
            
            
            
            
            
            .template addPattern<PointerToUpdateAccuracyPenaltyFunction, CodePattern{"55 48 89 E5 41 54 53 48 89 FB 48 83 EC 10 E8 ? ? ? ? 48 85 C0 0F 84"}>()
            
            
            
            
            
            
            
            
            
            
            .template addPattern<PointerToGetAimPunchFunction, CodePattern{"55 48 8D 4F 5C 48 89 E5 41 55 44 0F B6 EA 41 54 48 8D 57 50 49 89 F4 45 89 E9 53 4D 89 E0 48 89 FB"}>()
            
            
            
            
            
            
            
            
            
            
            
            
            
            
            
            
            
            
            
            
            
            
            .template addPattern<PointerToRegenerateWeaponSkin, CodePattern{"55 48 89 E5 41 57 41 56 49 89 FE 41 55 41 54 49 89 F4 53 48 81 EC 88 02 00 00"}>()
            
            
            
            
            
            
            
            
            
            
            
            
            .template addPattern<PointerToSetAttributeValueByName, CodePattern{"55 48 89 E5 53 48 89 FB 48 8D BF 10 11 00 00 48 83 EC 08 E8 ? ? ? ? 48 89 DF 48 8B 5D F8 C9 E9 ? ? ? ?"}>()
            
            
            
            
            
            
            
            
            
            
            
            .template addPattern<PointerToResolveSubclassData, CodePattern{"55 48 89 E5 41 57 49 89 FF 41 56 41 55 41 54 53 48 81 EC 48 01 00 00 48 8B 07 FF 90 ? ? ? ? 89 C3 41 8B 87 F0 04 00 00 85 C0"}>()
            .template addPattern<PointerToUpdateSkin, CodePattern{"48 8B 05 ? ? ? ? 80 78 58 00 75 ? C3 66 90 55 48 89 E5 41 56 41 55 41 54 41 89 F4 53 48 89 FB 48 81 EC A0 00 00 00"}>()
            .template addPattern<PointerToUpdateCompositeMaterialSet, CodePattern{"55 48 89 E5 53 48 89 FB 48 83 EC 18 8B 87 A0 02 00 00 85 C0 7E ? 48 89 F7 84 D2 75 ? 48 8D B3 A0 02 00 00 48 8B 5D F8 31 D2 C9"}>()
            
            
            
            
            
            
            
            
            
            
            
            
            
            .template addPattern<PointerToUpdateWeaponData, CodePattern{"55 48 89 E5 41 55 41 54 41 89 F4 53 48 89 FB 48 83 EC 08 83 FE 01 74 18"}>()
            
            
            
            
            
            
            
            
            
            
            
            
            
            
            
            
            
            
            
            
            
            
            
            
            
            
            .template addPattern<PointerToUpdateSubclass, CodePattern{"CC CC CC CC CC CC CC CC 55 48 89 E5 41 57 41 56 41 55 41 54 49 89 F4 53 48 89 FB 48 81 EC A8 00 00 00 E8 ? ? ? ? 48 89 DF E8"}.add(8)>()
            
            
            
            
            
            
            
            
            
            
            .template addPattern<PointerToGetItemDefinitionByIndex, CodePattern{"55 48 89 E5 41 54 53 48 89 FB 48 83 EC 10 48 8B 05 ? ? ? ? 48 85 C0 74 26 48 8B 78 08 48 85 FF 74 75 0F B7 B3 C2 10 00 00"}>()
            
            
            
            
            
            
            
            
            .template addPattern<PointerToSetModel, CodePattern{"55 48 89 E5 53 48 89 FB 48 83 EC 08 48 8D 05 ? ? ? ? 48 8B 38 48 8B 07 FF 50 68"}>()
            
            
            
            
            
            
            
            .template addPattern<PointerToGetPaintKitDefinition, CodePattern{"55 48 89 E5 53 48 83 EC 18 48 8B 07 FF 50 ? 89 C6 48 8B 05 ? ? ? ? 48 85 C0 74 ? 48 8B 78 ? 48 8B 5D"}>()
            
            
            
            
            
            
            
            
            
            .template addPattern<PointerToUpdateCompositeMaterial, CodePattern{"55 48 89 E5 41 57 41 56 41 55 49 89 FD 41 54 53 48 83 EC 28 89 75"}>();
    }
};
