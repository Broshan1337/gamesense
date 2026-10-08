#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>

#include <CS2/Classes/CPlantedC4.h>
#include <CS2/Classes/CCSGOInput.h>
#include <CS2/Classes/CUserCmd.h>
#include <Features/Game/MovementConfigVariables.h>
#include <GameClient/Bind.h>
#include <GameClient/Entities/C4.h>
#include <GameClient/Entities/PlayerPawn.h>
#include <GameClient/Entities/PlantedC4.h>
#include <GameClient/UserCmd.h>
#include <HookContext/HookContextMacros.h>
#include <Utils/Optional.h>
















template <typename HookContext>
class LastTickDefuse {
public:
    explicit LastTickDefuse(HookContext& hookContext) noexcept
        : hookContext{hookContext}
    {
    }

    void onCreateMove(cs2::CUserCmd* cmd) const noexcept
    {
        reset();

        if (!GET_CONFIG_VAR(last_tick_vars::Enabled))
            return;

        const UserCmd userCmd{cmd};
        if (!userCmd)
            return;

        const int bindValue = GET_CONFIG_VAR(last_tick_vars::DefuseKey);
        if (bindValue <= Bind::kOff || bindValue > Bind::kLast || !Bind::isDown(bindValue))
            return;

        auto&& localPawn = hookContext.localPlayerController().pawn().template as<PlayerPawn>();
        if (!localPawn)
            return;
        const auto health = localPawn.health();
        if (!health.hasValue() || health.value() <= 0)
            return;

        tickDefuse();
        tickPlant(localPawn);
    }

    void onWriteMoveCrc(cs2::CUserCmd* cmd) const noexcept
    {
        if (!cmd || (!pendingUse && !pendingAttack))
            return;

        const UserCmd userCmd{cmd};
        if (pendingUse)
            userCmd.setButtonState(kUseButton, true);
        if (pendingAttack)
            userCmd.setButtonState(cs2::CCSGOInput::Buttons::kAttack, true);
    }

    void onUnload() const noexcept
    {
        reset();
    }

private:
    void reset() const noexcept
    {
        pendingUse = false;
        pendingAttack = false;
    }

    

    void tickDefuse() const noexcept
    {
        
        auto&& bomb = hookContext.template make<PlantedC4>(hookContext.plantedC4Raw());
        if (!bomb.baseEntity())
            return;

        
        
        if (!bomb.isTicking().valueOr(true))
            return;
        if (bomb.isBeingDefused())
            return;

        const auto timeToBlow = bomb.getTimeToExplosion();
        if (!timeToBlow.hasValue())
            return;

        
        
        const bool hasDefuser = readHasDefuser();
        const float defuseSeconds = hasDefuser ? 5.2f : 10.2f;
        if (timeToBlow.value() > 0.0f && timeToBlow.value() <= defuseSeconds)
            pendingUse = true;
    }

    [[nodiscard]] bool readHasDefuser() const noexcept
    {
        auto&& localPawn = hookContext.localPlayerController().pawn().template as<PlayerPawn>();
        if (!localPawn)
            return false;

        const auto servicesOffset = hookContext.schemaSystem().getFieldOffset("C_CSPlayerPawn", "m_pItemServices");
        if (!servicesOffset.has_value() || *servicesOffset <= 0)
            return false;

        const void* itemServices = nullptr;
        std::memcpy(&itemServices, reinterpret_cast<const std::byte*>(static_cast<cs2::C_BaseEntity*>(localPawn.baseEntity())) + *servicesOffset, sizeof(itemServices));
        if (!itemServices)
            return false;

        const auto hasDefuserOffset = hookContext.schemaSystem().getFieldOffset("CCSPlayer_ItemServices", "m_bHasDefuser");
        if (!hasDefuserOffset.has_value() || *hasDefuserOffset <= 0)
            return false;

        std::uint8_t value{};
        std::memcpy(&value, reinterpret_cast<const std::byte*>(itemServices) + *hasDefuserOffset, sizeof(value));
        return value != 0;
    }

    

    void tickPlant(auto&& localPawn) const noexcept
    {
        auto&& activeWeapon = localPawn.getActiveWeapon();
        auto&& c4 = activeWeapon.template cast<C4>();
        if (!c4)
            return;

        auto* const c4Entity = static_cast<cs2::C_C4*>(static_cast<cs2::C_BaseEntity*>(c4.baseWeapon().baseEntity()));
        if (!c4Entity)
            return;

        const auto startedArming = schemaBool("C_C4", "m_bStartedArming", c4Entity);
        if (!startedArming.has_value())
            return;

        
        if (startedArming.value()) {
            const auto bombPlanted = schemaBool("C_C4", "m_bBombPlanted", c4Entity);
            if (!bombPlanted.has_value() || !bombPlanted.value())
                pendingUse = pendingAttack = true;
            return;
        }

        
        auto&& gameRules = hookContext.gameRules();
        if (gameRules.isRoundOver().valueOr(true))
            return;
        
        
        if (gameRules.isFreezePeriod().value_or(true) || gameRules.isWarmupPeriod().value_or(true))
            return;

        const auto roundEndTime = gameRules.roundEndTime();
        const auto curtime = hookContext.globalVars().curtime();
        if (!roundEndTime.hasValue() || !curtime.hasValue())
            return;

        constexpr float kPlantThreshold = 3.35f; 
        const float remaining = roundEndTime.value() - curtime.value();
        if (remaining > 0.0f && remaining <= kPlantThreshold)
            pendingUse = pendingAttack = true;
    }

    [[nodiscard]] std::optional<bool> schemaBool(const char* className, const char* fieldName, const void* object) const noexcept
    {
        if (!object)
            return {};
        const auto offset = hookContext.schemaSystem().getFieldOffset(className, fieldName);
        if (!offset.has_value() || *offset <= 0)
            return {};
        std::uint8_t value{};
        std::memcpy(&value, reinterpret_cast<const std::byte*>(object) + *offset, sizeof(value));
        return value != 0;
    }

    static constexpr std::uint64_t kUseButton = 0x20; 

    inline static bool pendingUse{false};
    inline static bool pendingAttack{false};

    HookContext& hookContext;
};