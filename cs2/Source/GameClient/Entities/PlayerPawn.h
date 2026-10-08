#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>

#include <CS2/Classes/Color.h>
#include <CS2/Classes/Entities/C_CSPlayerPawn.h>
#include <CS2/Classes/Entities/CCSPlayerController.h>
#include <CS2/Classes/ConVarTypes.h>
#include <CS2/Classes/Vector.h>
#include <GameClient/PawnSettle.h>
#include <Utils/Optional.h>
#include <Utils/Trig.h>
#include <GameClient/Entities/TeamNumber.h>
#include <GameClient/EntitySystem/EntitySystem.h>
#include <MemoryPatterns/PatternTypes/PlayerPawnPatternTypes.h>
#include <MemoryPatterns/PatternTypes/WeaponPatternTypes.h>
#include <Utils/ColorUtils.h>

#include "BaseEntity.h"
#include "C4.h"
#include "HostageServices.h"
#include "WeaponServices.h"

class EntityFromHandleFinder;

template <typename HookContext>
class PlayerController;

template <typename HookContext>
class PlayerPawn {
public:
    using RawType = cs2::C_CSPlayerPawn;

    PlayerPawn(HookContext& hookContext, cs2::C_CSPlayerPawn* playerPawn) noexcept
        : hookContext{hookContext}
        , playerPawn{playerPawn}
    {
    }

    [[nodiscard]] decltype(auto) baseEntity() const noexcept
    {
        return hookContext.template make<BaseEntity>(playerPawn);
    }

    [[nodiscard]] explicit operator bool() const noexcept
    {
        return playerPawn != nullptr;
    }

    
    [[nodiscard]] cs2::C_CSPlayerPawn* rawPawn() const noexcept
    {
        return playerPawn;
    }

    template <template <typename...> typename EntityType>
    [[nodiscard]] decltype(auto) cast() const noexcept
    {
        if (baseEntity().template is<EntityType>())
            return hookContext.template make<EntityType<HookContext>>(static_cast<typename EntityType<HookContext>::RawType*>(playerPawn));
        return hookContext.template make<EntityType<HookContext>>(nullptr);
    }

    [[nodiscard]] decltype(auto) weaponServices() const noexcept
    {
        cs2::CCSPlayer_WeaponServices* services = nullptr;
        if (playerPawn) {
            const auto offset = hookContext.schemaSystem().getFieldOffset("C_BasePlayerPawn", "m_pWeaponServices");
            if (offset.has_value() && *offset > 0)
                std::memcpy(&services, reinterpret_cast<const std::byte*>(playerPawn) + *offset, sizeof(services));
        }
        return hookContext.template make<WeaponServices>(services);
    }

    
    
    
    
    
    
    
    
    
    [[nodiscard]] Optional<cs2::Vector> aimPunchAngle() const noexcept
    {
        if (!playerPawn)
            return {};

        
        
        
        
        
        
        if (!pawn_settle::ready(playerPawn))
            return {};

        const auto servicesOffset = hookContext.schemaSystem().getFieldOffset("C_CSPlayerPawn", "m_pAimPunchServices");
        if (!servicesOffset.has_value() || *servicesOffset <= 0)
            return {};

        void* services = nullptr;
        std::memcpy(&services, reinterpret_cast<const std::byte*>(playerPawn) + *servicesOffset, sizeof(services));
        if (!services)
            return {};

        const auto getAimPunch = hookContext.patternSearchResults().template get<PointerToGetAimPunchFunction>();
        if (!getAimPunch)
            return {};

        
        
        
        
        const auto tick = hookContext.localPlayerController().tickBase();
        if (!tick.hasValue() || tick.value() <= 0)
            return {};
        PunchAccumulator sampleTime{tick.value(), 0.0f};
        // Query the read-only punch getter, never the recoil updater called by weapons.
        const auto punch = getAimPunch(services, &sampleTime, false);
        if (!__builtin_isfinite(punch.pitch) || !__builtin_isfinite(punch.yaw) || !__builtin_isfinite(punch.roll))
            return {};
        return cs2::Vector{punch.pitch, punch.yaw, punch.roll};
    }

    
    
    
    
    [[nodiscard]] Optional<int> shotsFired() const noexcept
    {
        if (!playerPawn)
            return {};

        const auto offset = hookContext.schemaSystem().getFieldOffset("C_CSPlayerPawn", "m_iShotsFired");
        if (!offset.has_value() || *offset <= 0)
            return {};

        int value{};
        std::memcpy(&value, reinterpret_cast<const std::byte*>(playerPawn) + *offset, sizeof(value));
        return value;
    }

    
    
    
    [[nodiscard]] Optional<cs2::Vector> eyePosition() const noexcept
    {
        const auto origin = baseEntity().absOrigin();
        if (!origin.hasValue())
            return {};

        const auto offset = hookContext.schemaSystem().getFieldOffset("C_BasePlayerPawn", "m_vecViewOffset");
        if (!offset.has_value() || *offset <= 0)
            return {};

        cs2::Vector viewOffset{};
        std::memcpy(&viewOffset, reinterpret_cast<const std::byte*>(static_cast<cs2::C_BaseEntity*>(baseEntity())) + *offset, sizeof(viewOffset));
        return cs2::Vector{origin.value().x + viewOffset.x, origin.value().y + viewOffset.y, origin.value().z + viewOffset.z};
    }

    
    
    
    
    [[nodiscard]] decltype(auto) hudModelArms() const noexcept
    {
        const auto handle = hookContext.hudModelArmsOffset().hudModelArms.of(playerPawn).valueOr(cs2::CEntityHandle{cs2::INVALID_EHANDLE_INDEX});
        return hookContext.template make<BaseEntity>(static_cast<cs2::C_BaseEntity*>(hookContext.template make<EntitySystem>().getEntityFromHandle(handle)));
    }

    [[nodiscard]] decltype(auto) weapons() const noexcept
    {
        return weaponServices().weapons();
    }

    [[nodiscard]] TeamNumber teamNumber() const noexcept
    {
        return baseEntity().teamNumber();
    }

    [[nodiscard]] std::optional<bool> isAlive() const noexcept
    {
        return baseEntity().isAlive();
    }

    [[nodiscard]] decltype(auto) playerController() const noexcept
    {
        const auto playerControllerHandle = hookContext.patternSearchResults().template get<OffsetToPlayerController>().of(playerPawn).get();
        if (!playerControllerHandle)
            return hookContext.template make<PlayerController>(nullptr);
        return hookContext.template make<PlayerController>(static_cast<cs2::CCSPlayerController*>(hookContext.template make<EntitySystem>().getEntityFromHandle(*playerControllerHandle)));
    }

    [[nodiscard]] auto health() const noexcept
    {
        return baseEntity().health();
    }

    [[nodiscard]] auto hasImmunity() const noexcept
    {
        return hookContext.patternSearchResults().template get<OffsetToPlayerPawnImmunity>().of(playerPawn).toOptional();
    }

    [[nodiscard]] decltype(auto) absOrigin() const noexcept
    {
        return baseEntity().absOrigin();
    }

    [[nodiscard]] bool isControlledByLocalPlayer() const noexcept
    {
        return playerController() == hookContext.localPlayerController();
    }

    [[nodiscard]] std::optional<bool> isEnemy() const noexcept
    {
        return teamNumber() != hookContext.localPlayerController().teamNumber() || teammatesAreEnemies();
    }

    [[nodiscard]] bool isTTorCT() const noexcept
    {
        const auto _teamNumber = teamNumber();
        return _teamNumber == TeamNumber::TT || _teamNumber == TeamNumber::CT;
    }

    [[nodiscard]] auto isPickingUpHostage() const noexcept
    {
        return hookContext.patternSearchResults().template get<OffsetToIsPickingUpHostage>().of(playerPawn).toOptional();
    }

    [[nodiscard]] auto isDefusing() const noexcept
    {
        return hookContext.patternSearchResults().template get<OffsetToIsDefusing>().of(playerPawn).toOptional();
    }

    [[nodiscard]] auto isScoped() const noexcept
    {
        return hookContext.patternSearchResults().template get<OffsetToIsScoped>().of(playerPawn).toOptional();
    }

    [[nodiscard]] bool isRescuingHostage() const noexcept
    {
        return hostageServices().hasCarriedHostage();
    }

    [[nodiscard]] bool isCarryingC4() const noexcept
    {
        return weapons().template has<C4>();
    }

    [[nodiscard]] decltype(auto) carriedC4() const noexcept
    {
        return weapons().template get<C4>();
    }

    [[nodiscard]] float getRemainingFlashBangTime() const noexcept
    {
        const auto curTime = hookContext.globalVars().curtime();
        if (!curTime.hasValue())
            return 0.0f;
        const auto flashBangEndTime = hookContext.patternSearchResults().template get<OffsetToFlashBangEndTime>().of(playerPawn).get();
        if (!flashBangEndTime)
            return 0.0f;
        if (*flashBangEndTime <= curTime.value())
            return 0.0f;
        return *flashBangEndTime - curTime.value();
    }

    [[nodiscard]] decltype(auto) getActiveWeapon() const noexcept
    {
        return weaponServices().getActiveWeapon();
    }

    
    
    [[nodiscard]] Optional<bool> isOnGround() const noexcept
    {
        const auto flagsOffset = hookContext.schemaSystem().getFieldOffset("C_BaseEntity", "m_fFlags");
        if (!flagsOffset.has_value() || *flagsOffset <= 0)
            return {};
        std::uint32_t flags{};
        std::memcpy(&flags, reinterpret_cast<const std::byte*>(playerPawn) + *flagsOffset, sizeof(flags));
        return (flags & (1u << 0)) != 0;
    }

    
    
    
    
    
    
    [[nodiscard]] bool isAtMaxAccuracy() const noexcept
    {
        auto&& weapon = getActiveWeapon();
        weapon.updateAccuracyPenalty();
        const auto inaccuracy = weapon.inaccuracy();
        if (!inaccuracy.hasValue())
            return false;

        const auto flagsOffset = hookContext.schemaSystem().getFieldOffset("C_BaseEntity", "m_fFlags");
        const auto velocityOffset = hookContext.schemaSystem().getFieldOffset("C_BaseEntity", "m_vecAbsVelocity");
        if (!flagsOffset.has_value() || *flagsOffset <= 0 || !velocityOffset.has_value() || *velocityOffset <= 0)
            return false;

        auto* const bytes = reinterpret_cast<const std::byte*>(playerPawn);
        std::uint32_t flags{};
        std::memcpy(&flags, bytes + *flagsOffset, sizeof(flags));
        cs2::Vector velocity{};
        std::memcpy(&velocity, bytes + *velocityOffset, sizeof(velocity));

        
        
        const bool onGround = (flags & (1u << 0)) != 0;
        const bool ducking = (flags & (1u << 1)) != 0;
        const float speed2d = trig::squareRoot(velocity.x * velocity.x + velocity.y * velocity.y);
        return weapon.isMaxAccuracy(inaccuracy.value(), onGround, ducking, isScoped().valueOr(false), speed2d);
    }

    [[nodiscard]] auto getSceneObjectUpdater() const noexcept
    {
        return reinterpret_cast<std::uint64_t(*)(cs2::C_CSPlayerPawn*, void*, bool)>(sceneObjectUpdaterHandle() ? sceneObjectUpdaterHandle()->updaterFunction : nullptr);
    }

    void setSceneObjectUpdater(auto x) const noexcept
    {
        if (sceneObjectUpdaterHandle())
            sceneObjectUpdaterHandle()->updaterFunction = reinterpret_cast<std::uint64_t(*)(void*, void*, bool)>(x);
    }

    [[nodiscard]] decltype(auto) isUsingSniperRifle() const
    {
        return getActiveWeapon().isSniperRifle();
    }

private:
    [[nodiscard]] auto sceneObjectUpdaterHandle() const noexcept
    {
        return hookContext.patternSearchResults().template get<OffsetToPlayerPawnSceneObjectUpdaterHandle>().of(playerPawn).valueOr(nullptr);
    }

    [[nodiscard]] decltype(auto) hostageServices() const noexcept
    {
        return hookContext.template make<HostageServices>(hookContext.patternSearchResults().template get<OffsetToHostageServices>().of(playerPawn).valueOr(nullptr));
    }

    [[nodiscard]] bool teammatesAreEnemies() const noexcept
    {
        return hookContext.cvarSystem().template getConVarValue<cs2::mp_teammates_are_enemies>().value_or(true);
    }

    HookContext& hookContext;
    cs2::C_CSPlayerPawn* playerPawn;
};
