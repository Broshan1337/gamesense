#pragma once

#include <cstdint>

#include <Features/Visuals/GrenadeTimers/GrenadeTimersConfigVariables.h>
#include <GameClient/Entities/BaseEntity.h>
#include <GameClient/Entities/Inferno.h>
#include <GameClient/Entities/SmokeGrenadeProjectile.h>
#include <GameClient/GlobalVars.h>
#include <GameClient/WorldToScreen/WorldToClipSpaceConverter.h>
#include <HookContext/HookContextMacros.h>
#include <UI/ImGui/OverlayLayer.h>















template <typename HookContext>
class GrenadeTimers {
public:
    explicit GrenadeTimers(HookContext& hookContext) noexcept
        : hookContext{hookContext}
    {
    }

    void beginFrame() const noexcept
    {
        pendingCount = 0;
    }

    void showSmoke(SmokeGrenadeProjectile<HookContext> smoke) const noexcept
    {
        if (!GET_CONFIG_VAR(grenade_timers_vars::Enabled) || !GET_CONFIG_VAR(grenade_timers_vars::SmokeTimers))
            return;
        if (!smoke.didSmokeEffect().valueOr(false))
            return;
        const auto tickBegin = smoke.smokeEffectTickBegin();
        if (!tickBegin.hasValue() || tickBegin.value() <= 0)
            return;
        
        
        constexpr auto kSmokeDuration = 18.0f;
        addTimer(smoke.baseEntity(), kSmokeDuration, tickBegin.value(), kSmokeColor);
    }

    void showInferno(Inferno<HookContext> inferno) const noexcept
    {
        if (!GET_CONFIG_VAR(grenade_timers_vars::Enabled) || !GET_CONFIG_VAR(grenade_timers_vars::MolotovTimers))
            return;
        const auto tickBegin = inferno.fireEffectTickBegin();
        const auto lifetime = inferno.fireLifetime();
        if (!tickBegin.hasValue() || tickBegin.value() <= 0 || !lifetime.hasValue() || lifetime.value() <= 0.0f)
            return;
        addTimer(inferno.baseEntity(), lifetime.value(), tickBegin.value(), kFireColor);
    }

    void endFrame() const noexcept
    {
        overlay_layer::publishTimers(frameTimers, pendingCount);
    }

private:
    static constexpr std::uint32_t kSmokeColor = 0xDCDCFFE3; 
    static constexpr std::uint32_t kFireColor = 0xFF9432E3;  

    void addTimer(auto&& entity, float totalDuration, int tickBegin, std::uint32_t rgba) const noexcept
    {
        const auto tickCount = hookContext.globalVars().tickCount();
        if (!tickCount.hasValue())
            return;
        const float interval = hookContext.globalVars().tickInterval().valueOr(0.015625f);
        const float remaining = totalDuration - static_cast<float>(tickCount.value() - tickBegin) * interval;
        if (remaining <= 0.0f)
            return;

        const auto origin = entity.absOrigin();
        if (!origin.hasValue())
            return;
        const auto clip = WorldToClipSpaceConverter{hookContext}.toClipSpace(origin.value());
        if (!clip.onScreen())
            return;
        
        
        
        const auto ndc = clip.toNormalizedDeviceCoordinates();

        if (pendingCount >= overlay_layer::kMaxTimers)
            return;
        frameTimers[pendingCount++] = overlay_layer::Timer{
            ndc.getX().m_flValue,
            ndc.getY().m_flValue,
            remaining,
            rgba};
    }

    inline static overlay_layer::Timer frameTimers[overlay_layer::kMaxTimers];
    inline static int pendingCount = 0;

    HookContext& hookContext;
};
