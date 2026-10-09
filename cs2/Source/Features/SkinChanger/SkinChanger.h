#pragma once

#include <cstdint>

#include <CS2/Classes/Entities/WeaponEntities.h>
#include <CS2/Classes/EntitySystem/CEntityHandle.h>
#include <CS2/Econ/ItemDefinitionIndex.h>
#include <CS2/Econ/PaintKitIndex.h>
#include <Features/SkinChanger/SkinChangerData.h>
#include <GameClient/Entities/BaseWeapon.h>
#include <GameClient/Entities/EntityClassifier.h>
#include <GameClient/EntitySystem/EntitySystem.h>
#include <GameClient/PawnSettle.h>
#include <Utils/CrashLogger.h>
#include <Utils/MurmurHash2.h>





[[nodiscard]] inline std::uint32_t makeSubclassToken(std::uint16_t defIndex) noexcept
{
    char buffer[6];
    int length = 0;

    
    
    do {
        buffer[length++] = static_cast<char>('0' + defIndex % 10);
        defIndex /= 10;
    } while (defIndex != 0);

    for (int i = 0, j = length - 1; i < j; ++i, --j) {
        const char temp = buffer[i];
        buffer[i] = buffer[j];
        buffer[j] = temp;
    }

    return murmurHash2Lower(buffer, length, 0x31415926);
}







template <typename HookContext>
class SkinChanger {
public:
    explicit SkinChanger(HookContext& hookContext) noexcept
        : hookContext{hookContext}
    {
    }

    void run() const noexcept
    {
        
        
        
        
        
        
        if (hookContext.gameRules().isRoundOver().valueOr(false))
            return;

        
        
        
        
        
        
        
        if (auto&& localPawn = hookContext.activeLocalPlayerPawn(); localPawn && localPawn.isAlive().value_or(false) && pawn_settle::ready(localPawn.rawPawn())) {
            
            
            
            
            
            
            
            localPawn.weaponServices().weapons().forEach([this, &localPawn](auto&& weaponEntity) {
                applyKnifeIfConfigured(localPawn, weaponEntity);
                applySkinIfConfigured(localPawn, weaponEntity.template as<BaseWeapon>());
            });
        }

        hookContext.skinChangerState().releaseDeadEntities([this](cs2::CEntityHandle handle) {
            return hookContext.template make<EntitySystem>().getEntityFromHandle(handle) != nullptr;
        });
    }

private:
    
    
    
    
    [[nodiscard]] const char* resolveKnifeModelPath(std::uint16_t defIndex) const noexcept
    {
        const auto getItemDefinition = hookContext.patternSearchResults().template get<PointerToGetItemDefinitionByIndex>();
        if (!getItemDefinition)
            return nullptr;

        alignas(std::max_align_t) static unsigned char indexHolder[4300]{};
        *reinterpret_cast<std::uint16_t*>(indexHolder + 4290) = defIndex;

        const auto itemDefinition = getItemDefinition(indexHolder);
        if (!itemDefinition)
            return nullptr;

        return *reinterpret_cast<const char* const*>(reinterpret_cast<const unsigned char*>(itemDefinition) + 328);
    }

    
    
    
    
    
    template <typename F>
    void withHudWeapon(auto&& localPawn, cs2::CEntityHandle weaponHandle, F f) const noexcept
    {
        localPawn.hudModelArms().forEachChild([&](auto&& child) {
            if (child.ownerHandle().valueOr(cs2::CEntityHandle{cs2::INVALID_EHANDLE_INDEX}) == weaponHandle)
                f(child.template as<BaseWeapon>());
        });
    }

    
    
    
    
    void applyKnifeIfConfigured(auto&& localPawn, auto&& weaponEntity) const noexcept
    {
        if (weaponEntity.classify().typeIndex != EntityTypeInfo::indexOf<cs2::C_Knife>())
            return;

        const auto configuredKnife = SkinChangerData::configuredKnifeModel(hookContext);
        if (!configuredKnife.has_value())
            return;

        const auto desiredDefIndex = static_cast<int>(*configuredKnife);
        
        
        
        const int configuredFinish = SkinChangerData::configuredKnifePaintKitId(hookContext);
        const auto desiredPaintKit = configuredFinish;

        
        
        
        if (desiredPaintKit != 0 && !SkinChangerData::isKnifePaintKitValid(configuredKnife, desiredPaintKit))
            return;

        const auto [configuredWearPermille, configuredSeed] = SkinChangerData::configuredKnifeWearAndSeed(hookContext);
        const int statTrak = GET_CONFIG_VAR(skin_changer_vars::StatTrakEnabled) ? GET_CONFIG_VAR(skin_changer_vars::StatTrakValue) : 0;

        const auto knife = weaponEntity.template as<BaseWeapon>();
        const auto handle = knife.baseEntity().handle();

        
        
        
        
        
        
        
        
        
        
        
        
        static cs2::CEntityHandle lastActiveWeaponHandle{cs2::INVALID_EHANDLE_INDEX};
        const auto activeHandle = localPawn.weaponServices().activeWeaponHandle().valueOr(cs2::CEntityHandle{cs2::INVALID_EHANDLE_INDEX});
        const bool justEquipped = activeHandle == handle && lastActiveWeaponHandle != handle;
        lastActiveWeaponHandle = activeHandle;

        
        
        
        
        
        
        
        
        
        
        
        
        
        
        
        
        
        
        const auto desiredSubclassToken = makeSubclassToken(static_cast<std::uint16_t>(*configuredKnife));

        const bool alreadyInDesiredState =
            knife.itemDefinitionIndex().valueOr(0) == desiredDefIndex
            && knife.subclassID().valueOr(0) == desiredSubclassToken
            && knife.paintKit().valueOr(-1) == desiredPaintKit
            && knife.seed().valueOr(-1) == configuredSeed;

        
        
        
        
        
        if (!justEquipped && alreadyInDesiredState)
            return;

        
        
        
        
        
        
        
        
        
        
        
        
        
        
        
        
        
        
        
        static float lastReapplyAttemptTime = -1000.0f;
        const auto curtime = hookContext.globalVars().curtime();
        if (!curtime.hasValue())
            return;
        constexpr float kReapplyIntervalSeconds = 0.15f;
        const bool intervalElapsed = curtime.value() - lastReapplyAttemptTime >= kReapplyIntervalSeconds;
        if (!justEquipped && !intervalElapsed)
            return;
        lastReapplyAttemptTime = curtime.value();

        const char* modelPath = resolveKnifeModelPath(static_cast<std::uint16_t>(*configuredKnife));

        
        
        
        
        
        
        
        
        
        
        
        
        
        
        
        
        static std::uint64_t lastSetModelHandle = 0;
        const auto currentHandle = knife.modelHandle();
        if (currentHandle != 0 && currentHandle == lastSetModelHandle) {
            knife.setItemDefinitionIndex(desiredDefIndex);
        } else {
            CrashLogger::trace(0x340); 
            knife.setModel(modelPath); 
            CrashLogger::trace(0x341);
            lastSetModelHandle = knife.modelHandle();
            knife.setItemDefinitionIndex(desiredDefIndex);
        }
        
        
        
        
        if (!knife.applySkinAttributes(desiredPaintKit, configuredSeed, static_cast<float>(configuredWearPermille) / 1000.0f))
            return;

        
        
        
        knife.setEntityQuality(statTrak ? 9 : 3);
        if (statTrak)
            knife.setStatTrak(statTrak);
        knife.updateWeaponData(1);
        knife.setSubclassID(desiredSubclassToken);

        
        
        
        
        
        
        
        
        
        
        
        
        
        
        
        
        
        
        
        
        
        
        
        
        
        
        
        
        
        
        
        
        
        
        
        
        
        
        
        
        knife.resolveSubclassData();
        knife.updateSubclass();

        
        
        
        const std::uint64_t meshMask = knife.isEquippedPaintKitLegacy() ? 2 : 1;
        knife.setMeshGroupMask(meshMask);

        
        
        
        withHudWeapon(localPawn, handle, [modelPath, meshMask](auto&& hudWeapon) {
            hudWeapon.setModel(modelPath);
            hudWeapon.setMeshGroupMask(meshMask);
        });

        
        
        
        
        
        
        
        
        
        
        
        
        
        
        
        
        
        knife.updateCompositeMaterial();

        if (!knife.regenerateSkin())
            return;
        hookContext.skinChangerState().markApplied(handle, desiredPaintKit, configuredSeed, configuredWearPermille, statTrak);

        
        
        
        
        
        
        knife.baseEntity().gameSceneNode().postDataUpdate();

        
        
        
        
        
        
        
        
        
        
        
        
        
        
        
        
        
        
        
        
        
        
        
        
        
        
    }

    void applySkinIfConfigured(auto&& localPawn, auto&& weapon) const noexcept
    {
        const auto defIndex = weapon.itemDefinitionIndex();
        if (!defIndex.hasValue())
            return;

        
        
        
        const auto configuredKit = SkinChangerData::configuredPaintKitId(hookContext, static_cast<cs2::ItemDefinitionIndex>(defIndex.value()));
        if (!configuredKit.has_value())
            return;

        
        
        
        if (*configuredKit != 0 && !SkinChangerData::isPaintKitValidFor(static_cast<cs2::ItemDefinitionIndex>(defIndex.value()), *configuredKit))
            return;

        const auto handle = weapon.baseEntity().handle();

        
        
        
        const auto originalSeed = weapon.seed().valueOr(0);
        const auto originalWear = weapon.wear().valueOr(0.0f);
        hookContext.skinChangerState().rememberOriginalPaintKit(handle,
            static_cast<int>(weapon.paintKit().valueOr(0)),
            originalSeed,
            static_cast<int>(originalWear * 1000.0f + 0.5f));

        
        
        
        
        
        
        const auto [configuredWearPermille, configuredSeed] = SkinChangerData::configuredWearAndSeed(hookContext, static_cast<cs2::ItemDefinitionIndex>(defIndex.value()));
        const int statTrak = GET_CONFIG_VAR(skin_changer_vars::StatTrakEnabled) ? GET_CONFIG_VAR(skin_changer_vars::StatTrakValue) : 0;

        const auto original = hookContext.skinChangerState().originalState(handle).value_or(SkinChangerState::OriginalState{0, 0, 10});
        const int desiredPaintKit = *configuredKit != 0 ? *configuredKit : original.paintKit;
        const int desiredSeed = *configuredKit != 0 ? configuredSeed : original.seed;
        const int desiredWearPermille = *configuredKit != 0 ? configuredWearPermille : original.wearPermille;

        if (hookContext.skinChangerState().alreadyApplied(handle, desiredPaintKit, desiredSeed, desiredWearPermille, statTrak))
            return;

        
        
        
        
        if (!weapon.applySkinAttributes(desiredPaintKit, desiredSeed, static_cast<float>(desiredWearPermille) / 1000.0f))
            return;

        
        if (statTrak) {
            weapon.setEntityQuality(9);
            weapon.setStatTrak(statTrak);
        }

        
        
        
        
        
        
        
        
        weapon.updateWeaponData(0);

        
        
        
        
        
        
        const std::uint64_t meshMask = weapon.isEquippedPaintKitLegacy() ? 2 : 1;
        weapon.setMeshGroupMask(meshMask);
        withHudWeapon(localPawn, handle, [meshMask](auto&& hudWeapon) {
            hudWeapon.setMeshGroupMask(meshMask);
        });

        
        
        
        
        weapon.updateCompositeMaterial();

        if (!weapon.regenerateSkin())
            return;
        hookContext.skinChangerState().markApplied(handle, desiredPaintKit, desiredSeed, desiredWearPermille, statTrak);

        
        
        weapon.baseEntity().gameSceneNode().postDataUpdate();
    }

    HookContext& hookContext;
};
