#pragma once

#include <cstring>

#include <CS2/Classes/Entities/C_BaseEntity.h>
#include <CS2/Classes/Entities/C_CSPlayerPawn.h>
#include <Features/Game/RevealRadarConfigVariables.h>
#include <GameClient/Entities/BaseEntity.h>
#include <GameClient/Entities/PlayerPawn.h>
#include <GameClient/EntitySystem/EntitySystem.h>
#include <HookContext/HookContextMacros.h>

// skeet's "reveal radar" (memory reference_pastoskeet_dump, sub_18005F5D0): for every alive enemy
// pawn, set the client copy of EntitySpottedState_t::m_bSpotted (uint8 at state+0x8). The client
// radar draws whoever it believes is spotted, so the enemies show up on the minimap without the
// server's spotting rules ever running. Purely visual/client-side - the server copy is untouched
// and nothing here can trip a server-side check. Like the reference (and every entity mutator in
// this project) it runs on the GAME thread from CreateMove.
//
// m_bSpottedByMask is deliberately NOT written: the reference sets only m_bSpotted, and the radar
// path reads exactly that flag.
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
    // EntitySpottedState_t::m_bSpotted (schema-confirmed layout: the bool sits at +0x8 inside the
    // embedded state struct, m_bSpottedByMask follows at +0xC).
    static constexpr std::ptrdiff_t kSpottedOffset = 0x8;

    HookContext& hookContext;
};
