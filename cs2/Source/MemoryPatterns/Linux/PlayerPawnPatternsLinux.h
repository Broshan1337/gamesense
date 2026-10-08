#pragma once

#include <MemoryPatterns/PatternTypes/PlayerPawnPatternTypes.h>
#include <MemorySearch/CodePattern.h>

struct PlayerPawnPatterns {
    [[nodiscard]] static consteval auto addClientPatterns(auto clientPatterns) noexcept
    {
        return clientPatterns
            
            
            
            .template addPattern<OffsetToPlayerPawnImmunity, CodePattern{"0F B6 BC 24 ? ? ? ? 45 84 FF 75 2D"}.add(4).read()>()
            .template addPattern<OffsetToWeaponServices, CodePattern{"48 8B BE ? ? ? ? 48 8D 35 ? ? ? ? E8 ? ? ? ? 48 89 C2"}.add(3).read()>()
            .template addPattern<OffsetToPlayerController, CodePattern{"D0 89 87 ? ? ? ? 48 8B 07"}.add(3).read()>()
            
            
            
            .template addPattern<OffsetToIsDefusing, CodePattern{"? ? ? ? 01 41 80 BF E1 06 00 00"}.read()>()
            .template addPattern<OffsetToIsPickingUpHostage, CodePattern{"? ? ? ? 00 74 ? C6 86 ? ? ? ? 00 4C"}.read()>()
            .template addPattern<OffsetToHostageServices, CodePattern{"74 0E 48 8B BB ? ? ? ? 31"}.add(5).read()>()
            
            
            
            .template addPattern<OffsetToFlashBangEndTime, CodePattern{"0F 10 87 ? ? ? ? F3 0F 5C 05"}.add(3).read()>()
            .template addPattern<OffsetToPlayerPawnSceneObjectUpdaterHandle, CodePattern{"89 83 ? ? ? ? 48 8B BB ? ? ? ? 48 8B"}.add(2).read()>()
            .template addPattern<OffsetToIsScoped, CodePattern{"BB ? ? ? ? 00 F3 0F 11 45 ? 0F"}.add(1).read()>();
    }
};
