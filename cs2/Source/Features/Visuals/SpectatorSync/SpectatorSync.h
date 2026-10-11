#pragma once

#include <cstdint>
#include <cstring>

#include <CS2/Classes/Entities/C_BaseEntity.h>
#include <Features/Visuals/SpectatorSync/SpectatorSyncConfigVariables.h>
#include <GameClient/Entities/BaseEntity.h>
#include <GameClient/Entities/PlayerPawn.h>
#include <GameClient/EntitySystem/EntitySystem.h>
#include <HookContext/HookContextMacros.h>

// Spectator sync + threat module (batch H, vesta semantics): queries the Esp
// producer consumes. `beingSpectated()` = any alive pawn's observer target is
// us (SpectatorList's proven chain); the Esp producer then hides the player
// presentation while observed (spectator sync - never draw wall knowledge a
// live spectator could see). `threatFlags()` = per-enemy danger states the Esp
// colors/highlights: FLASHED (blinded), SCOPED (rifle scoped = watching an
// angle), DEFUSING (bomb urgency), close-range (weapon-type aware).
template <typename HookContext>
class SpectatorSync {
public:
    explicit SpectatorSync(HookContext& hookContext) noexcept
        : hookContext{hookContext}
    {
    }

    // Refresh once per Esp frame (game thread, before the Esp walk).
    void update() const noexcept
    {
        observed = false;
        if (!GET_CONFIG_VAR(spectator_sync_vars::Enabled) || !GET_CONFIG_VAR(spectator_sync_vars::HideWhileSpectated))
            return;

        auto&& localPawn = hookContext.activeLocalPlayerPawn();
        if (!localPawn)
            return;
        const auto localHandle = hookContext.template make<BaseEntity>(localPawn.baseEntity()).handle().value;
        if (!localHandle)
            return;
        const auto servicesOffset = hookContext.schemaSystem().getFieldOffset("C_BasePlayerPawn", "m_pObserverServices");
        const auto targetOffset = hookContext.schemaSystem().getFieldOffset("CPlayer_ObserverServices", "m_hObserverTarget");
        if (!servicesOffset.has_value() || *servicesOffset <= 0 || !targetOffset.has_value() || *targetOffset <= 0)
            return;

        hookContext.template make<EntitySystem>().forEachNetworkableEntityIdentity([&](const auto& entityIdentity) {
            if (observed)
                return;
            const auto entityTypeInfo = hookContext.entityClassifier().classifyEntity(entityIdentity.entityClass);
            if (!entityTypeInfo.template is<cs2::C_CSPlayerPawn>())
                return;
            auto&& pawn = hookContext.template make<PlayerPawn>(static_cast<cs2::C_CSPlayerPawn*>(entityIdentity.entity));
            const auto alive = pawn.isAlive();
            if (!alive.has_value() || !*alive)
                return;
            std::byte* services{};
            std::memcpy(&services, reinterpret_cast<const std::byte*>(static_cast<cs2::C_BaseEntity*>(pawn.baseEntity())) + *servicesOffset, sizeof(services));
            if (!services)
                return;
            std::uint32_t target{};
            std::memcpy(&target, services + *targetOffset, sizeof(target));
            if (target == localHandle)
                observed = true;
        });
    }

    [[nodiscard]] bool beingSpectated() const noexcept
    {
        return observed;
    }

    [[nodiscard]] bool threatEnabled() const noexcept
    {
        return GET_CONFIG_VAR(spectator_sync_vars::Enabled) && GET_CONFIG_VAR(spectator_sync_vars::ThreatHighlights);
    }

    void onUnload() const noexcept
    {
        observed = false;
    }

private:
    HookContext& hookContext;
    inline static bool observed{false};
};