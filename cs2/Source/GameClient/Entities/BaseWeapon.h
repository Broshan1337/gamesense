#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include <CS2/Classes/CAttributeList.h>
#include <CS2/Classes/CEconItemAttribute.h>
#include <CS2/Classes/CModelState.h>
#include <CS2/Classes/Entities/C_CSWeaponBase.h>
#include <CS2/Classes/CCSWeaponBaseVData.h>
#include <CS2/Classes/Vector.h>
#include <MemoryPatterns/PatternTypes/WeaponPatternTypes.h>
#include <MemoryPatterns/PatternTypes/WeaponVDataPatternTypes.h>
#include <Utils/RetAddrSpoofer.h>
#include "BaseEntity.h"
#include "EntityClassifier.h"

template <typename HookContext>
class BaseWeapon {
public:
    BaseWeapon(HookContext& hookContext, cs2::C_CSWeaponBase* baseWeapon) noexcept
        : hookContext{hookContext}
        , baseWeapon{baseWeapon}
    {
    }

    using RawType = cs2::C_CSWeaponBase;

    template <template <typename...> typename EntityType>
    [[nodiscard]] bool is() const noexcept
    {
        return baseEntity().template is<EntityType>();
    }

    template <template <typename...> typename EntityType>
    [[nodiscard]] decltype(auto) cast() const noexcept
    {
        return baseEntity().template cast<EntityType>();
    }

    [[nodiscard]] decltype(auto) baseEntity() const noexcept
    {
        return hookContext.template make<BaseEntity>(baseWeapon);
    }

    [[nodiscard]] bool isSniperRifle() const noexcept
    {
        switch (baseEntity().classify().typeIndex) {
        case EntityTypeInfo::indexOf<cs2::C_WeaponSSG08>():
        case EntityTypeInfo::indexOf<cs2::C_WeaponAWP>():
        case EntityTypeInfo::indexOf<cs2::C_WeaponG3SG1>():
        case EntityTypeInfo::indexOf<cs2::C_WeaponSCAR20>(): return true;
        default: return false;
        }
    }

    [[nodiscard]] auto bulletInaccuracy() const noexcept
    {
        return inaccuracy() + spread();
    }

    [[nodiscard]] auto getName() const noexcept
    {
        const auto vData = static_cast<cs2::CCSWeaponBaseVData*>(hookContext.template make<BaseEntity>(baseWeapon).vData().valueOr(nullptr));
        return hookContext.patternSearchResults().template get<OffsetToWeaponName>().of(vData).valueOr(nullptr);
    }

    [[nodiscard]] auto clipAmmo() const noexcept
    {
        return hookContext.patternSearchResults().template get<OffsetToClipAmmo>().of(baseWeapon).toOptional();
    }

    [[nodiscard]] auto paintKit() const noexcept
    {
        return hookContext.econEntityOffsets().fallbackPaintKit.of(baseWeapon).toOptional();
    }

    void setPaintKit(int paintKit) const noexcept
    {
        hookContext.econEntityOffsets().fallbackPaintKit.of(baseWeapon) = paintKit;
    }

    [[nodiscard]] auto seed() const noexcept
    {
        return hookContext.econEntityOffsets().fallbackSeed.of(baseWeapon).toOptional();
    }

    void setSeed(int seed) const noexcept
    {
        hookContext.econEntityOffsets().fallbackSeed.of(baseWeapon) = seed;
    }

    [[nodiscard]] auto wear() const noexcept
    {
        return hookContext.econEntityOffsets().fallbackWear.of(baseWeapon).toOptional();
    }

    void setWear(float wear) const noexcept
    {
        hookContext.econEntityOffsets().fallbackWear.of(baseWeapon) = wear;
    }

    [[nodiscard]] auto statTrak() const noexcept
    {
        return hookContext.econEntityOffsets().fallbackStatTrak.of(baseWeapon).toOptional();
    }

    void setStatTrak(int statTrak) const noexcept
    {
        hookContext.econEntityOffsets().fallbackStatTrak.of(baseWeapon) = statTrak;
    }

    
    
    
    [[nodiscard]] auto itemDefinitionIndex() const noexcept
    {
        const auto& offsets = hookContext.econItemAttributeOffsets();
        const auto attributeContainer = offsets.attributeManager.of(baseWeapon).get();
        const auto item = offsets.item.of(attributeContainer).get();
        return offsets.itemDefinitionIndex.of(item).toOptional();
    }

    
    
    
    
    
    
    
    
    void setItemDefinitionIndex(int defIndex) const noexcept
    {
        const auto& offsets = hookContext.econItemAttributeOffsets();
        const auto attributeContainer = offsets.attributeManager.of(baseWeapon).get();
        const auto item = offsets.item.of(attributeContainer).get();
        if (!item)
            return;
        offsets.itemDefinitionIndex.of(item) = defIndex;
    }

    
    
    
    void setEntityQuality(int quality) const noexcept
    {
        const auto& offsets = hookContext.econItemAttributeOffsets();
        const auto attributeContainer = offsets.attributeManager.of(baseWeapon).get();
        const auto item = offsets.item.of(attributeContainer).get();
        if (!item)
            return;
        offsets.entityQuality.of(item) = quality;
    }

    
    
    
    
    
    
    void setSubclassID(std::uint32_t hash) const noexcept
    {
        hookContext.entitySubclassOffsets().subclassID.of(baseWeapon) = hash;
    }

    
    
    
    
    [[nodiscard]] auto subclassID() const noexcept
    {
        return hookContext.entitySubclassOffsets().subclassID.of(baseWeapon).toOptional();
    }

    
    
    
    
    
    void resolveSubclassData() const noexcept
    {
        if (const auto resolve = hookContext.patternSearchResults().template get<PointerToResolveSubclassData>(); resolve && baseWeapon)
            resolve(baseWeapon);
    }

    
    
    
    
    
    
    void updateWeaponData(unsigned int changeType) const noexcept
    {
        if (const auto update = hookContext.patternSearchResults().template get<PointerToUpdateWeaponData>(); update && baseWeapon)
            update(baseWeapon, changeType);
    }

    
    
    
    
    void updateSubclass() const noexcept
    {
        if (const auto update = hookContext.patternSearchResults().template get<PointerToUpdateSubclass>(); update && baseWeapon) {
            
            
            
            
            
            if (*static_cast<const unsigned char*>(update.rawAddress()) == 0xCC)
                return;
            update(baseWeapon, nullptr);
        }
    }

    
    
    
    
    
    
    
    
    
    
    void setMeshGroupMask(std::uint64_t mask) const noexcept
    {
        const auto& offsets = hookContext.modelStateOffsets();
        const auto bodyComponent = offsets.bodyComponent.of(baseWeapon).valueOr(nullptr);
        if (!bodyComponent)
            return;

        
        
        const auto skeletonInstance = offsets.skeletonInstance.of(reinterpret_cast<cs2::CBodyComponentSkeletonInstance*>(bodyComponent)).get();
        if (!skeletonInstance)
            return;
        const auto modelState = offsets.modelState.of(skeletonInstance).get();
        if (!modelState)
            return;
        offsets.meshGroupMask.of(modelState) = mask;
    }

    
    
    
    
    
    [[nodiscard]] std::uint64_t modelHandle() const noexcept
    {
        const auto& offsets = hookContext.modelStateOffsets();
        const auto bodyComponent = offsets.bodyComponent.of(baseWeapon).valueOr(nullptr);
        if (!bodyComponent)
            return 0;

        
        
        const auto skeletonInstance = offsets.skeletonInstance.of(reinterpret_cast<cs2::CBodyComponentSkeletonInstance*>(bodyComponent)).get();
        if (!skeletonInstance)
            return 0;
        const auto modelState = offsets.modelState.of(skeletonInstance).get();
        if (!modelState)
            return 0;

        std::uint64_t handle = 0;
        return offsets.modelHandle.of(modelState).valueOr(handle);
    }

    
    
    
    
    
    
    
    [[nodiscard]] bool isEquippedPaintKitLegacy() const noexcept
    {
        const auto& offsets = hookContext.econItemAttributeOffsets();
        const auto attributeContainer = offsets.attributeManager.of(baseWeapon).get();
        const auto item = offsets.item.of(attributeContainer).get();
        if (!item)
            return false;

        const auto getPaintKitDefinition = hookContext.patternSearchResults().template get<PointerToGetPaintKitDefinition>();
        if (!getPaintKitDefinition)
            return false;

        const auto paintKitDefinition = getPaintKitDefinition(item);
        if (!paintKitDefinition)
            return false;

        return *(reinterpret_cast<const unsigned char*>(paintKitDefinition) + 174) != 0;
    }

    
    
    void setModel(const char* modelPath) const noexcept
    {
        if (const auto setModelFn = hookContext.patternSearchResults().template get<PointerToSetModel>(); setModelFn && baseWeapon && modelPath)
            setModelFn(baseWeapon, modelPath);
    }

    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    bool applySkinAttributes(int paintKit, int seed, float wear) const noexcept
    {
        const auto& offsets = hookContext.econItemAttributeOffsets();
        const auto attributeContainer = offsets.attributeManager.of(baseWeapon).get();
        const auto item = offsets.item.of(attributeContainer).get();

        const auto setAttribute = hookContext.patternSearchResults().template get<PointerToSetAttributeValueByName>();
        if (!setAttribute || !item)
            return false;

        
        
        
        
        
        
        
        
        
        
        offsets.itemIDHigh.of(item) = 0xF0000000u;
        offsets.itemIDLow.of(item) = 0x10u;
        offsets.initialized.of(item) = true;
        offsets.accountID.of(item) = offsets.originalOwnerXuidLow.of(baseWeapon).valueOr(0u);
        offsets.disallowSOC.of(item) = true;
        offsets.restoreCustomMaterialAfterPrecache.of(item) = true;

        offsets.entityQuality.of(item) = 0;

        setAttribute(item, "set item texture prefab", static_cast<float>(paintKit));
        setAttribute(item, "set item texture wear", wear);
        setAttribute(item, "set item texture seed", static_cast<float>(seed));

        
        
        
        
        
        
        
        
        
        
        hookContext.econEntityOffsets().fallbackPaintKit.of(baseWeapon) = paintKit;
        hookContext.econEntityOffsets().fallbackSeed.of(baseWeapon) = seed;
        hookContext.econEntityOffsets().fallbackWear.of(baseWeapon) = wear;
        return true;
    }

    
    
    
    
    
    
    
    
    
    
    
    
    void updateCompositeMaterial() const noexcept
    {
        const auto update = hookContext.patternSearchResults().template get<PointerToUpdateCompositeMaterial>();
        if (!update || !baseWeapon)
            return;

        update(reinterpret_cast<unsigned char*>(baseWeapon) + cs2::C_CSWeaponBase::kCompositeMaterialOwnerOffset, true);
    }

    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    bool regenerateSkin() const noexcept
    {
        if (!baseEntity().vData().valueOr(nullptr))
            return false;

        
        
        
        
        
        
        
        
        
        
        
        
        if (const auto regenerate = hookContext.patternSearchResults().template get<PointerToRegenerateWeaponSkin>(); regenerate && baseWeapon)
            regenerate(baseWeapon, false);

        return true;
    }

    [[nodiscard]] auto getSceneObjectUpdater() const noexcept
    {
        return reinterpret_cast<std::uint64_t(*)(cs2::C_CSWeaponBase*, void*, bool)>(sceneObjectUpdaterHandle() ? sceneObjectUpdaterHandle()->updaterFunction : nullptr);
    }

    void setSceneObjectUpdater(auto x) const noexcept
    {
        if (sceneObjectUpdaterHandle())
            sceneObjectUpdaterHandle()->updaterFunction = reinterpret_cast<std::uint64_t(*)(void*, void*, bool)>(x);
    }

    
    
    
    
    
    
    
    void updateAccuracyPenalty() const noexcept
    {
        if (const auto updateFn = hookContext.patternSearchResults().template get<PointerToUpdateAccuracyPenaltyFunction>(); updateFn && baseWeapon)
            updateFn(baseWeapon);
    }

    
    
    
    
    
    [[nodiscard]] Optional<float> inaccuracy() const noexcept
    {
        const auto getInaccuracyFn = hookContext.patternSearchResults().template get<PointerToGetInaccuracyFunction>();
        if (baseWeapon && getInaccuracyFn)
            return getInaccuracyFn(baseWeapon, nullptr, nullptr);
        return {};
    }

    
    
    
    
    
    
    
    
    
    
    
    
    [[nodiscard]] Optional<float> inaccuracyAtVelocity(cs2::C_BaseEntity* pawn, const cs2::Vector& velocity) const noexcept
    {
        const auto getInaccuracyFn = hookContext.patternSearchResults().template get<PointerToGetInaccuracyFunction>();
        const auto updateFn = hookContext.patternSearchResults().template get<PointerToUpdateAccuracyPenaltyFunction>();
        if (!baseWeapon || !pawn || !getInaccuracyFn || !updateFn)
            return {};

        auto&& schema = hookContext.schemaSystem();
        
        
        
        const auto turningDeltaOffset = schema.getFieldOffset("C_CSWeaponBase", "m_flTurningInaccuracyDelta");
        const auto recoilIndexOffset = schema.getFieldOffset("C_CSWeaponBase", "m_flRecoilIndex");
        const auto velocityOffset = schema.getFieldOffset("C_BaseEntity", "m_vecAbsVelocity");
        const auto eflagsOffset = schema.getFieldOffset("C_BaseEntity", "m_iEFlags");
        if (!turningDeltaOffset.has_value() || !recoilIndexOffset.has_value() || !velocityOffset.has_value() || !eflagsOffset.has_value())
            return {};
        if (*turningDeltaOffset <= 0 || *recoilIndexOffset < *turningDeltaOffset || *velocityOffset <= 0 || *eflagsOffset <= 0)
            return {};

        const std::size_t accuracyStateSize = static_cast<std::size_t>(*recoilIndexOffset - *turningDeltaOffset) + sizeof(float);
        if (accuracyStateSize > kMaxAccuracyStateSize)
            return {};

        auto* const weaponBytes = reinterpret_cast<std::byte*>(baseWeapon);
        auto* const pawnBytes = reinterpret_cast<std::byte*>(pawn);

        std::byte backup[kMaxAccuracyStateSize];
        std::memcpy(backup, weaponBytes + *turningDeltaOffset, accuracyStateSize);

        cs2::Vector oldVelocity{};
        std::memcpy(&oldVelocity, pawnBytes + *velocityOffset, sizeof(oldVelocity));
        std::uint32_t oldEflags{};
        std::memcpy(&oldEflags, pawnBytes + *eflagsOffset, sizeof(oldEflags));

        const std::uint32_t newEflags = oldEflags & ~0x1000u;
        std::memcpy(pawnBytes + *eflagsOffset, &newEflags, sizeof(newEflags));
        std::memcpy(pawnBytes + *velocityOffset, &velocity, sizeof(velocity));

        updateFn(baseWeapon);
        const float predicted = getInaccuracyFn(baseWeapon, nullptr, nullptr);

        std::memcpy(pawnBytes + *velocityOffset, &oldVelocity, sizeof(oldVelocity));
        std::memcpy(pawnBytes + *eflagsOffset, &oldEflags, sizeof(oldEflags));
        std::memcpy(weaponBytes + *turningDeltaOffset, backup, accuracyStateSize);

        return predicted;
    }

    [[nodiscard]] Optional<float> spread() const noexcept
    {
        const auto getSpreadFn = hookContext.patternSearchResults().template get<PointerToGetSpreadFunction>();
        if (baseWeapon && getSpreadFn)
            return getSpreadFn(baseWeapon);
        return {};
    }

    
    
    
    
    
    [[nodiscard]] Optional<int> numBullets() const noexcept
    {
        const auto vData = baseEntity().vData().valueOr(nullptr);
        if (!vData)
            return {};
        int value{};
        std::memcpy(&value, reinterpret_cast<const std::byte*>(vData) + kNumBulletsOffset, sizeof(value));
        return value;
    }

    
    
    
    [[nodiscard]] Optional<float> vDataFloat(const char* fieldName) const noexcept
    {
        const auto vData = baseEntity().vData().valueOr(nullptr);
        if (!vData)
            return {};
        const auto offset = hookContext.schemaSystem().getFieldOffset("CCSWeaponBaseVData", fieldName);
        if (!offset.has_value() || *offset <= 0)
            return {};
        float value{};
        std::memcpy(&value, reinterpret_cast<const std::byte*>(vData) + *offset, sizeof(value));
        return value;
    }

    
    
    [[nodiscard]] Optional<float> maxSpeed() const noexcept
    {
        return vDataFloat("m_flMaxSpeed");
    }

    
    
    
    [[nodiscard]] Optional<float> attackMovespeedFactor() const noexcept
    {
        return vDataFloat("m_flAttackMovespeedFactor");
    }

    
    
    [[nodiscard]] Optional<float> inaccuracyJumpApex() const noexcept
    {
        return vDataFloat("m_flInaccuracyJumpApex");
    }

    
    
    
    [[nodiscard]] Optional<float> baseDamage() const noexcept
    {
        const auto vData = baseEntity().vData().valueOr(nullptr);
        if (!vData)
            return {};
        const auto offset = hookContext.schemaSystem().getFieldOffset("CCSWeaponBaseVData", "m_nDamage");
        if (!offset.has_value() || *offset <= 0)
            return {};
        int value{};
        std::memcpy(&value, reinterpret_cast<const std::byte*>(vData) + *offset, sizeof(value));
        return static_cast<float>(value);
    }

    [[nodiscard]] Optional<float> rangeModifier() const noexcept
    {
        return vDataFloat("m_flRangeModifier");
    }

    
    
    [[nodiscard]] Optional<float> penetrationPower() const noexcept
    {
        return vDataFloat("m_flPenetration");
    }

    [[nodiscard]] Optional<float> armorRatio() const noexcept
    {
        return vDataFloat("m_flArmorRatio");
    }

    [[nodiscard]] Optional<float> headshotMultiplier() const noexcept
    {
        return vDataFloat("m_flHeadshotMultiplier");
    }

    
    
    [[nodiscard]] Optional<float> accuracyPenalty() const noexcept
    {
        if (!baseWeapon)
            return {};
        const auto offset = hookContext.schemaSystem().getFieldOffset("C_CSWeaponBase", "m_fAccuracyPenalty");
        if (!offset.has_value() || *offset <= 0)
            return {};
        float value{};
        std::memcpy(&value, reinterpret_cast<const std::byte*>(baseWeapon) + *offset, sizeof(value));
        return value;
    }

    
    
    
    
    
    
    
    
    [[nodiscard]] bool isMaxAccuracy(float inaccuracy, bool onGround, bool isDucking, bool isScoped, float speed2d) const noexcept
    {
        if (onGround) {
            if (isSniperRifle()) {
                if (!isScoped)
                    return false;

                
                if (isDucking) {
                    const float rounded = static_cast<float>(static_cast<int>(inaccuracy * 300.0f)) / 300.0f;
                    return rounded < inaccuracy;
                }
                if (speed2d <= 0.1f) {
                    const float rounded = static_cast<float>(static_cast<int>(inaccuracy * 170.0f)) / 170.0f;
                    return rounded < inaccuracy;
                }
                return false;
            }

            const auto weaponMaxSpeed = maxSpeed();
            if (!weaponMaxSpeed.hasValue())
                return false;
            return speed2d <= weaponMaxSpeed.value() * 0.34f;
        }

        const auto jumpApex = inaccuracyJumpApex();
        const auto penalty = accuracyPenalty();
        if (!jumpApex.hasValue() || !penalty.hasValue())
            return false;
        constexpr float tolerance = 0.001f;
        return inaccuracy <= penalty.value() + jumpApex.value() + tolerance;
    }

    
    
    
    
    [[nodiscard]] Optional<float> recoilIndex() const noexcept
    {
        if (!baseWeapon)
            return {};
        float value{};
        std::memcpy(&value, reinterpret_cast<const std::byte*>(baseWeapon) + kRecoilIndexOffset, sizeof(value));
        return value;
    }

private:
    
    
    static constexpr std::ptrdiff_t kNumBulletsOffset = 0x730;
    static constexpr std::ptrdiff_t kRecoilIndexOffset = 0x28B8;

    
    
    
    static constexpr std::size_t kMaxAccuracyStateSize = 64;

    [[nodiscard]] auto sceneObjectUpdaterHandle() const noexcept
    {
        return hookContext.patternSearchResults().template get<OffsetToWeaponSceneObjectUpdaterHandle>().of(baseWeapon).valueOr(nullptr);
    }

    HookContext& hookContext;
    cs2::C_CSWeaponBase* baseWeapon;
};
