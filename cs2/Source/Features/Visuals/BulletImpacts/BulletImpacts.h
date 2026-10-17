#pragma once

#include <cstdint>

#include <CS2/Classes/Entities/C_BaseEntity.h>
#include <CS2/Classes/IGameEventManager2.h>
#include <CS2/Classes/IGameEventManager2.h>
#include <CS2/Classes/Vector.h>
#include <Features/Visuals/BulletImpacts/BulletImpactsConfigVariables.h>
#include <GameClient/GameEvents/GameEventFields.h>
#include <GameClient/SchemaSystem/SchemaReadiness.h>
#include <GameClient/WorldToScreen/WorldToClipSpaceConverter.h>
#include <HookContext/HookContextMacros.h>
#include <Utils/ColorUtils.h>
#include <UI/ImGui/OverlayLayer.h>

// Bullet impact markers (2026-10-15, friend request "add a feature for bullet
// impacts, cs2 has a command for these"): sv_showimpacts is SERVER-side (needs
// sv_cheats + host); this is the client-side equivalent - every bullet_impact
// event (the same event the tracers consume) records a world-space impact
// point, and the render path projects + publishes each live marker as an
// overlay circle that fades over the configured lifetime.
template <typename HookContext>
class BulletImpacts {
public:
    explicit BulletImpacts(HookContext& hookContext) noexcept
        : hookContext{hookContext}
    {
    }

    void onFireEventClientSide(cs2::IGameEvent* event) const noexcept
    {
        if (!GET_CONFIG_VAR(bullet_impacts_vars::Enabled))
            return;
        if (!event || !game_events::is(event, "bullet_impact"))
            return;
        if (!game_events::localPlayerIsSlot(hookContext, game_events::entityForKey(event, "userid")))
            return;

        const auto curtime = hookContext.globalVars().curtime();
        if (!curtime.hasValue() || curtime.value() < schema_readiness::kMinMapTime)
            return;

        const cs2::Vector impact{
            game_events::floatForKey(event, "x"),
            game_events::floatForKey(event, "y"),
            game_events::floatForKey(event, "z"),
        };
        impacts[writeIndex] = Impact{impact, curtime.value(), true};
        writeIndex = (writeIndex + 1) % kMaxImpacts;
    }

    // Re-projects every live marker each frame (the bolts pattern) into the
    // dedicated ImpactMarkers channel; age fades toward zero alpha.
    void run() const noexcept
    {
        if (!GET_CONFIG_VAR(bullet_impacts_vars::Enabled)) {
            if (wasActive)
                overlay_layer::publishImpactMarkers(nullptr, 0);
            return;
        }
        const auto curtime = hookContext.globalVars().curtime();
        if (!curtime.hasValue() || curtime.value() < schema_readiness::kMinMapTime) {
            if (wasActive)
                overlay_layer::publishImpactMarkers(nullptr, 0);
            return;
        }

        wasActive = true;
        auto&& converter = hookContext.template make<WorldToClipSpaceConverter>();
        const auto color = GET_CONFIG_VAR(bullet_impacts_vars::Color);
        const float lifetime = static_cast<float>(GET_CONFIG_VAR(bullet_impacts_vars::Lifetime)) / 100.0f;

        overlay_layer::ImpactMarker markers[overlay_layer::kMaxImpactMarkers];
        int published = 0;
        for (int i = 0; i < kMaxImpacts && published < overlay_layer::kMaxImpactMarkers; ++i) {
            auto& impact = impacts[(writeIndex + i) % kMaxImpacts];
            if (!impact.alive)
                continue;
            const float age = curtime.value() - impact.bornTime;
            if (age < 0.0f || age >= lifetime) {
                impact.alive = false;
                continue;
            }
            const auto clip = converter.toClipSpace(impact.position);
            if (clip.w < 0.001f)
                continue;
            const float xPercent = (clip.x / clip.w + 1.0f) * 50.0f;
            // Same convention as the bolts producer: 0 = top of screen.
            const float yPercent = (1.0f - clip.y / clip.w) * 50.0f;
            if (xPercent < 0.0f || xPercent > 100.0f || yPercent < 0.0f || yPercent > 100.0f)
                continue;
            const float fade = 1.0f - age / lifetime;
            markers[published++] = overlay_layer::ImpactMarker{
                xPercent, yPercent,
                (static_cast<std::uint32_t>(color.r() * fade) << 24)
                    | (static_cast<std::uint32_t>(color.g() * fade) << 16)
                    | (static_cast<std::uint32_t>(color.b() * fade) << 8)
                    | color.a(),
            };
        }
        overlay_layer::publishImpactMarkers(markers, published);
    }

    void onUnload() const noexcept
    {
        for (auto& impact : impacts)
            impact.alive = false;
        overlay_layer::publishImpactMarkers(nullptr, 0);
    }

private:
    struct Impact {
        cs2::Vector position;
        float bornTime;
        bool alive;
    };

    static constexpr int kMaxImpacts = 32;
    static constexpr float kMarkerRadiusPx = 4.0f;
    static constexpr float kMarkerThickness = 1.6f;

    inline static Impact impacts[kMaxImpacts]{};
    inline static int writeIndex = 0;
    inline static bool wasActive = false;

    HookContext& hookContext;
};