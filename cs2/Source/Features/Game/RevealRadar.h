#pragma once

#include <cstring>

#include <CS2/Classes/Entities/C_BaseEntity.h>
#include <CS2/Classes/Entities/C_CSPlayerPawn.h>
#include <Features/Game/RevealRadarConfigVariables.h>
#include <GameClient/Entities/BaseEntity.h>
#include <GameClient/Entities/PlayerPawn.h>
#include <GameClient/EntitySystem/EntitySystem.h>
#include <HookContext/HookContextMacros.h>










template <typename HookContext>
class RevealRadar {
public:
    explicit RevealRadar(HookContext& hookContext) noexcept
        : hookContext{hookContext}
    {
    }

    void run() const noexcept
    {
        if (!GET_CONFIG_VAR(reveal_radar_vars::Enabled))
            return;

        const auto stateOffset = hookContext.schemaSystem().getFieldOffset("C_CSPlayerPawn", "m_entitySpottedState");
        if (!stateOffset.has_value() || *stateOffset <= 0)
            return;

        hookContext.template make<EntitySystem>().forEachNetworkableEntityIdentity([&](const auto& identity) {
            auto&& baseEntity = hookContext.template make<BaseEntity>(static_cast<cs2::C_BaseEntity*>(identity.entity));
            if (!baseEntity.classify().template is<cs2::C_CSPlayerPawn>())
                return;
            auto&& pawn = baseEntity.template as<PlayerPawn>();
            if (!pawn || pawn.isControlledByLocalPlayer() || pawn.isEnemy() != true || pawn.isAlive() != true)
                return;

            auto* const entityBytes = reinterpret_cast<std::byte*>(static_cast<cs2::C_BaseEntity*>(pawn.baseEntity()));
            const std::uint8_t spotted = 1;
            std::memcpy(entityBytes + *stateOffset + kSpottedOffset, &spotted, sizeof(spotted));
        });
    }

private:
    
    
    static constexpr std::ptrdiff_t kSpottedOffset = 0x8;

    HookContext& hookContext;
};
