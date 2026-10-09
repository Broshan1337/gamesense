#pragma once

#include <MemoryPatterns/PatternTypes/ClientPatternTypes.h>
#include <MemorySearch/CodePattern.h>

struct ClientPatterns {
    [[nodiscard]] static consteval auto addClientPatterns(auto clientPatterns) noexcept
    {
        return clientPatterns
            .template addPattern<MainMenuPanelPointer, CodePattern{"E5 53 48 83 EC ? 48 8D 1D ? ? ? ? 48 8B 03 48 8B 78 ? 48 8B"}.add(9).abs()>()
            
            
            
            
            
            
            
            
            
            
            
            
            
            
            
            
            
            
            
            
            .template addPattern<GlobalVarsPointer, CodePattern{"8D ? ? ? ? ? 48 89 35 ? ? ? ? 48 89 ? ? C3"}.add(9).abs()>()
            .template addPattern<TransformTranslate3dVMT, CodePattern{"48 8D 0D ? ? ? ? 48 89 08 48 89 50 08 48 8B 53 10"}.add(3).abs()>()
            .template addPattern<TransformScale3dVMT, CodePattern{"48 8B 53 08 48 8D 0D ? ? ? ? F3 0F 10 43"}.add(7).abs()>()
            .template addPattern<WorldToProjectionMatrixPointer, CodePattern{"01 4C 8D 05 ? ? ? ? 4C 89 EE"}.add(4).abs()>()
            .template addPattern<ViewToProjectionMatrixPointer, CodePattern{"EE 48 8D 0D ? ? ? ? 48 8D 15 ? ? ? ? 48"}.add(4).abs()>()
            .template addPattern<ViewRenderPointer, CodePattern{"48 8D 05 ? ? ? ? 48 89 38 48 85"}.add(3).abs()>()
            .template addPattern<LocalPlayerControllerPointer, CodePattern{"48 83 3D ? ? ? ? ? 0F 95 C0 C3"}.add(3).abs(5)>()
            
            
            
            
            
            
            
            
            
            
            
            
            
            
            
            
            
            
            
            
            
            
            
            
            
            
            
            .template addPattern<GameEventManagerGlobalPointer, CodePattern{"4C 8D 3D ? ? ? ? 48 8D 3D ? ? ? ? E8 ? ? ? ? 4C 89 FF E8 ? ? ? ? 4C 89 FE 4C 89 E2 48 8D 3D"}.add(3).abs()>()
            
            
            
            
            
            
            
            
            .template addPattern<ChatPrintFunction, CodePattern{"55 48 89 E5 41 55 49 89 D5 41 54 41 89 F4 53 48 81 EC D8 10 00 00"}>()
            
            
            
            
            
            
            
            
            
            
            
            
            
            
            .template addPattern<ChangeTeammateColorCycle, CodePattern{"55 48 89 E5 41 54 53 48 8D 1D ? ? ? ? 48 83 EC 10 0F B6 05 ? ? ? ? 84 C0 74 ? 48 8B 05 ? ? ? ? BE FF FF FF FF 48 89 DF 44 8B 60 58 E8"}>()
            
            
            
            
            .template addPattern<HudChatDelegatePointer, CodePattern{"48 8B 05 ? ? ? ? C3 CC CC CC CC CC CC CC CC 55 48 89 E5 41 56 49 89"}.add(3).abs()>()
            
            
            
            
            
            .template addPattern<CSGOInputPointer, CodePattern{"4C 89 EF E8 ? ? ? ? 4C 8D 3D ? ? ? ? 4C 89 FF E8"}.add(11).abs()>()
            
            
            
            
            .template addPattern<PlayerRankingDataPointer, CodePattern{"48 8D 1D ? ? ? ? F6 43 10 04 0F 84"}.add(3).abs()>()
            
            
            
            
            
            
            
            .template addPattern<EconSystemAccessor, CodePattern{"E8 ? ? ? ? 48 8B 80 ? ? ? ? 48 85 C0 74 ? 48 8B 78 68 E8 ? ? ? ? 48 89 C3 48 85 C0 74 ? E8"}.add(1).abs()>()
            .template addPattern<GameAccountClientAccessor, CodePattern{"E8 ? ? ? ? 48 8B 80 ? ? ? ? 48 85 C0 74 ? 48 8B 78 68 E8 ? ? ? ? 48 89 C3 48 85 C0 74 ? E8"}.add(22).abs()>()
            
            
            
            
            
            
            
            .template addPattern<CreateSubtickMoveStep, CodePattern{"49 8B 7E 18 4C 89 85 ? ? ? ? E8 ? ? ? ? 49 8D 7E 18 48 89 C6 E8"}.add(12).abs()>()
            .template addPattern<RepeatedPtrFieldAddAllocated, CodePattern{"55 53 48 89 F5 51 48 8B 47 10 48 89 FB 48 85 C0 74 07 8B 4F 0C 39 08 75 0D BE 01 00 00 00"}>()
            
            
            
            
            .template addPattern<GetUserCmd, CodePattern{"55 48 89 E5 41 54 4C 8B 25 ? ? ? ? 53 89 F3"}>()
            
            
            
            
            .template addPattern<AbandonCooldownGate, CodePattern{"48 8B 46 18 48 85 C0 74 ? 48 8B 40 48 48 39 85 ? ? ? ? 74"}.add(20)>()
            .template addPattern<ManageGlowSceneObjectPointer, CodePattern{"55 66 48 0F 7E C8"}>()
            .template addPattern<SetSceneObjectAttributeFloat4, CodePattern{"55 66 0F 6E D6 48 89 E5 53 48"}>()
            .template addPattern<PointerToClientMode, CodePattern{"05 ? ? ? ? ? 89 ? 48 89 05 ? ? ? ? E8 ? ? ? ? ? 8B ? ? C9 C3"}.add(1).abs()>();
    }
};
