#pragma once

#include <cmath>
#include <cstdint>
#include <cstring>

#include <CS2/Classes/Entities/C_BaseEntity.h>
#include <CS2/Classes/Entities/C_BaseCSGrenadeProjectile.h>
#include <CS2/Classes/Entities/GrenadeProjectiles.h>
#include <CS2/Classes/Entities/C_Inferno.h>
#include <CS2/Classes/Vector.h>
#include <Features/Visuals/GrenadePrediction/GrenadePredictionConfigVariables.h>
#include <GameClient/Entities/BaseEntity.h>
#include <GameClient/EntitySystem/EntitySystem.h>
#include <GameClient/SchemaSystem/SchemaReadiness.h>
#include <HookContext/HookContextMacros.h>
#include <UI/ImGui/OverlayLayer.h>
#include <Features/Visuals/GrenadePrediction/GrenadePredictionConfigVariables.h>
#include <Utils/ColorUtils.h>
#include <Utils/Optional.h>

// Grenade projectile ESP (batch D, the lazy-correct cut): FLASH timer circles
// on live flashbang projectiles (time-to-blast = spawn+1.6s - curtime, ring
// drawn projected at the grenade), INFERNO bounds circles on burning molotovs
// (radius 150u around the fire origin - the per-fire-cell hull needs a live RE
// session), and SMOKE timers via the existing GrenadeTimers feature. A full
// simulated landing prediction (vesta's trajectory engine) needs the grenade
// physics stepped against our Tracing - deliberately deferred; the game's own
// m_arrTrajectoryTrailPoints gives the PAST path, which is worthless for a
// pre-throw lineup, so no fake.
template <typename HookContext>
class GrenadePredictionEsp {
public:
    explicit GrenadePredictionEsp(HookContext& hookContext) noexcept
        : hookContext{hookContext}
    {
    }

    void run() const noexcept
    {
        if (!GET_CONFIG_VAR(grenade_pred_vars::Enabled)) {
            if (wasActive) {
                overlay_layer::publishGrenades({});
                wasActive = false;
            }
            return;
        }

        auto&& localPawn = hookContext.activeLocalPlayerPawn();
        const auto curtime = hookContext.globalVars().curtime();
        if (!localPawn || !curtime.hasValue() || curtime.value() < schema_readiness::kMinMapTime) {
            overlay_layer::publishGrenades({});
            wasActive = false;
            return;
        }
        wasActive = true;

        overlay_layer::GrenadeScene scene;
        hookContext.template make<EntitySystem>().forEachNetworkableEntityIdentity([&](const auto& entityIdentity) {
            const auto entityTypeInfo = hookContext.entityClassifier().classifyEntity(entityIdentity.entityClass);
            if (entityTypeInfo.template is<cs2::C_Inferno>()) {
                if (!GET_CONFIG_VAR(grenade_pred_vars::InfernoBounds) || scene.infernoCount >= overlay_layer::kMaxGrenadeMarkers)
                    return;
                auto&& inferno = hookContext.template make<BaseEntity>(static_cast<cs2::C_BaseEntity*>(entityIdentity.entity));
                const auto origin = inferno.absOrigin();
                if (!origin.hasValue() || !finite(origin.value()))
                    return;
                scene.infernos[scene.infernoCount++] = overlay_layer::GrenadeMarker{origin.value().x, origin.value().y, origin.value().z, 0.0f};
                return;
            }
            if (!entityTypeInfo.template is<cs2::C_FlashbangProjectile>())
                return;
            auto&& projectile = hookContext.template make<BaseEntity>(static_cast<cs2::C_BaseEntity*>(entityIdentity.entity));
            if (!projectile)
                return;
            const auto origin = projectile.absOrigin();
            if (!origin.hasValue() || !finite(origin.value()))
                return;
            if (!GET_CONFIG_VAR(grenade_pred_vars::FlashTimer) || scene.flashCount >= overlay_layer::kMaxGrenadeMarkers)
                return;
            const auto spawnTime = projectileFloat(projectile.baseEntity(), "C_BaseCSGrenadeProjectile", "m_flSpawnTime");
            if (!spawnTime.hasValue())
                return;
            // Blast ~1.6s after spawn (sv_grenade_trajectory-consistent constant).
            const float timeLeft = spawnTime.value() + 1.6f - curtime.value();
            if (timeLeft <= 0.0f)
                return;
            scene.flashes[scene.flashCount++] = overlay_layer::GrenadeMarker{origin.value().x, origin.value().y, origin.value().z, timeLeft};
        });

        overlay_layer::publishGrenades(scene);
    }

    void onUnload() const noexcept
    {
        overlay_layer::publishGrenades({});
        wasActive = false;
    }

private:
    [[nodiscard]] static bool finite(const cs2::Vector& v) noexcept
    {
        return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
    }

    [[nodiscard]] Optional<float> projectileFloat(cs2::C_BaseEntity* entity, const char* className, const char* fieldName) const noexcept
    {
        const auto offset = hookContext.schemaSystem().getFieldOffset(className, fieldName);
        if (!offset.has_value() || *offset <= 0)
            return {};
        float value{};
        std::memcpy(&value, reinterpret_cast<const std::byte*>(entity) + *offset, sizeof(value));
        return value;
    }

    HookContext& hookContext;
    inline static bool wasActive{false};
};