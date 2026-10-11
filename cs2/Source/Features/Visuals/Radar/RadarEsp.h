#pragma once

#include <cmath>
#include <cstdint>
#include <cstring>

#include <CS2/Classes/Entities/C_CSPlayerPawn.h>
#include <CS2/Classes/Vector.h>
#include <Features/Visuals/Radar/RadarConfigVariables.h>
#include <GameClient/Entities/BaseEntity.h>
#include <GameClient/Entities/PlayerPawn.h>
#include <GameClient/EntitySystem/EntitySystem.h>
#include <GameClient/SchemaSystem/SchemaReadiness.h>
#include <HookContext/HookContextMacros.h>
#include <UI/ImGui/OverlayLayer.h>
#include <Utils/ColorUtils.h>
#include <Utils/Optional.h>
#include <Utils/Trig.h>

// Radar overlay (batch B): a self-contained north-up minimap window, drawn in
// renderGameOverlay. Positions are RELATIVE TO THE LOCAL PLAYER (no game
// overview struct / map KeyValues needed - the lazy correct design): dots for
// players (view cones), a bomb marker for the planted C4. World->radar scale =
// Zoom units per radar-pixel-band; the local player sits at the window center.
// The game's own radar transform (vesta's overview object math) needs a live
// RE session - deliberately skipped; this rides OverlayLayer publish/snapshot.
template <typename HookContext>
class RadarEsp {
public:
    explicit RadarEsp(HookContext& hookContext) noexcept
        : hookContext{hookContext}
    {
    }

    void run() const noexcept
    {
        if (!GET_CONFIG_VAR(radar_vars::Enabled)) {
            if (wasActive) {
                overlay_layer::publishRadar({});
                wasActive = false;
            }
            return;
        }

        auto&& localPawn = hookContext.activeLocalPlayerPawn();
        const auto curtime = hookContext.globalVars().curtime();
        if (!localPawn || !curtime.hasValue() || curtime.value() < schema_readiness::kMinMapTime) {
            overlay_layer::publishRadar({});
            wasActive = false;
            return;
        }
        const auto localOrigin = localPawn.absOrigin();
        if (!localOrigin.hasValue() || !finite(localOrigin.value())) {
            overlay_layer::publishRadar({});
            wasActive = false;
            return;
        }

        wasActive = true;
        overlay_layer::RadarScene scene;
        scene.zoom = static_cast<float>(GET_CONFIG_VAR(radar_vars::Zoom));
        scene.sizePx = static_cast<float>(GET_CONFIG_VAR(radar_vars::Size));
        scene.viewCones = GET_CONFIG_VAR(radar_vars::ViewCones);

        hookContext.template make<EntitySystem>().forEachNetworkableEntityIdentity([&](const auto& entityIdentity) {
            const auto entityTypeInfo = hookContext.entityClassifier().classifyEntity(entityIdentity.entityClass);
            if (entityTypeInfo.template is<cs2::C_CSPlayerPawn>()) {
                auto&& pawn = hookContext.template make<PlayerPawn>(static_cast<cs2::C_CSPlayerPawn*>(entityIdentity.entity));
                if (pawn.isControlledByLocalPlayer())
                    return;
                const auto alive = pawn.isAlive();
                if (!alive.has_value() || !*alive)
                    return;
                const auto origin = pawn.absOrigin();
                if (!origin.hasValue() || !finite(origin.value()))
                    return;
                const auto enemy = pawn.isEnemy();
                if (!enemy.has_value())
                    return;
                if (scene.dotCount >= overlay_layer::kMaxRadarDots)
                    return;
                const auto eyeAngles = pawnValue<cs2::Vector>(pawn.baseEntity(), "C_CSPlayerPawn", "m_angEyeAngles");
                overlay_layer::RadarDot& dot = scene.dots[scene.dotCount++];
                dot.xOffsetUnits = origin.value().x - localOrigin.value().x;
                dot.yOffsetUnits = origin.value().y - localOrigin.value().y;
                dot.enemy = *enemy;
                dot.yaw = eyeAngles.hasValue() ? eyeAngles.value().y : 0.0f;
            } else if (entityTypeInfo.template is<cs2::CPlantedC4>()) {
                auto&& planted = hookContext.template make<BaseEntity>(static_cast<cs2::C_BaseEntity*>(entityIdentity.entity));
                const auto origin = planted.absOrigin();
                if (!origin.hasValue() || !finite(origin.value()) || scene.hasBomb)
                    return;
                scene.hasBomb = true;
                scene.bombXOffsetUnits = origin.value().x - localOrigin.value().x;
                scene.bombYOffsetUnits = origin.value().y - localOrigin.value().y;
            }
        });

        overlay_layer::publishRadar(scene);
    }

    void onUnload() const noexcept
    {
        overlay_layer::publishRadar({});
        wasActive = false;
    }

private:
    [[nodiscard]] static bool finite(const cs2::Vector& v) noexcept
    {
        return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
    }

    template <typename T>
    [[nodiscard]] Optional<T> pawnValue(cs2::C_BaseEntity* entity, const char* className, const char* fieldName) const noexcept
    {
        const auto offset = hookContext.schemaSystem().getFieldOffset(className, fieldName);
        if (!offset.has_value() || *offset <= 0)
            return {};
        T value{};
        std::memcpy(&value, reinterpret_cast<const std::byte*>(entity) + *offset, sizeof(value));
        return value;
    }

    HookContext& hookContext;
    inline static bool wasActive{false};
};