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

// Smoke / molotov burn timers, rendered at the center of the area they describe. Driven per
// entity from RenderingHookEntityLoop (beginFrame -> per-entity show -> endFrame, the same
// collect-then-publish shape as BombPlantAlert): every active smoke projectile and burning
// inferno contributes one overlay_layer::Timer entry - remaining seconds projected at the
// entity's world origin, drawn by the present thread on the foreground draw list.
//
// Remaining time math mirrors the game's own inferno code (libclient reads
// m_nFireEffectTickBegin, converts with tick interval and subtracts from the current time):
//   remaining = total - (currentTick - tickBegin) * intervalPerTick
// where total = the fixed 18s smoke duration for smokes (no client field carries it) and
// C_Inferno's own m_nFireLifetime for molotovs/incendiaries.
//
// Every read is a pattern-resolved offset and every step null-guarded: a failed pattern or a
// missing global just means no timer that frame.
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
        // CS2 smokes burn a fixed 18 seconds (smoke_grenade_duration); the client struct has no
        // duration field, and the convar has not changed since the smoke rework.
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
    static constexpr std::uint32_t kSmokeColor = 0xDCDCFFE3; // pale white-blue, packed 0xRRGGBBAA
    static constexpr std::uint32_t kFireColor = 0xFF9432E3;  // molotov orange

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
        // behind the camera the projection flips - only draw what the converter marks on-screen.
        // getX()/getY() are the percent-of-screen values (0..100, y from the top) the overlay
        // layer's convention is built on.
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
