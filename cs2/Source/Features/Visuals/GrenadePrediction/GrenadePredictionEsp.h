#pragma once

#include <cmath>
#include <cstdint>
#include <cstring>

#include <CS2/Classes/Entities/C_BaseEntity.h>
#include <CS2/Classes/Entities/C_BaseCSGrenadeProjectile.h>
#include <CS2/Classes/Entities/C_Inferno.h>
#include <CS2/Classes/Entities/GrenadeProjectiles.h>
#include <CS2/Classes/Vector.h>
#include <Features/Combat/Autowall/Autowall.h>
#include <Features/Combat/ShotGeometry.h>
#include <Features/Visuals/GrenadePrediction/GrenadePredictionConfigVariables.h>
#include <GameClient/Entities/BaseEntity.h>
#include <GameClient/Entities/PlayerPawn.h>
#include <GameClient/EntitySystem/EntitySystem.h>
#include <GameClient/GlobalVars.h>
#include <GameClient/SchemaSystem/SchemaReadiness.h>
#include <GameClient/Tracing/Tracing.h>
#include <HookContext/HookContextMacros.h>
#include <UI/ImGui/OverlayLayer.h>
#include <Utils/Optional.h>

// Grenade ESP (batch D, rewritten 2026-10-13 after the in-game verdict):
//
// 1. TRAJECTORY PREDICTION while HOLDING a grenade - a live physics preview
//    (the original request: "render a line where the grenade would go and
//    land with like a circle"): thrown from the eye along the view with
//    full-throw speed + our velocity, gravity + bounces stepped through our
//    Tracing primitive (reflect about the surface normal, restitution 0.3).
//    Points + landing publish WORLD-space; the draw side projects.
// 2. FLASH TIMER on live flashbang projectiles (time to detonate).
// 3. INFERNO BOUNDS only while the fire is actually burning (the old version
//    drew the circle as long as the Inferno entity existed - the entity
//    outlives the flames): burning = curtime < fireEffectTickBegin*tick +
//    fireLifetime.
//
// THREADING: the whole scene (flash timers, inferno bounds, trajectory) is
// produced on the GAME thread (CreateMove) and published to the OverlayLayer -
// the trajectory reads m_angEyeAngles/m_vecVelocity and runs traces, all of
// which tear if read mid-tick on the render thread (the first cut ran the sim
// on the render thread and drew glitchy random-angle lines - 2026-10-13
// report). The render side only draws.
//
// All fail-closed: no session / no eye / no weapon data = no-op.
namespace grenade_detail
{

inline constexpr int kMaxTrajectoryPoints = 64;
inline constexpr float kGravity = 320.0f;      // sv_gravity * 0.4 - the community-verified
                                               // CS grenade projectile gravity (800 = the
                                               // player gravity; grenades fall at 0.4x - the
                                               // old 800 made the preview plunge 2.5x too
                                               // fast, the "long line going down" report)
inline constexpr float kRestitution = 0.45f;   // velocity kept per bounce
inline constexpr float kTick = 0.015625f;     // 64-tick
inline constexpr float kMaxSimTime = 3.0f;
inline constexpr int kMaxBounces = 4;
inline constexpr float kFullThrowSpeed = 675.0f;
inline constexpr float kStopSpeed = 30.0f;

[[nodiscard]] inline bool finite(const cs2::Vector& v) noexcept
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

// One physics step: integrate, trace the step, reflect on hit. Returns true if
// a bounce landed the projectile ON the ground (nearly-flat normal + slow).
template <typename Tracer>
inline bool step(cs2::Vector& position, cs2::Vector& velocity, void* skipEntity,
                 cs2::Vector& outPoint, Tracer&& tracer) noexcept
{
    const cs2::Vector next{position.x + velocity.x * kTick,
                           position.y + velocity.y * kTick,
                           position.z + (velocity.z - kGravity * kTick) * kTick};
    if (!finite(next))
        return false;
    const auto trace = tracer(position, next, skipEntity);
    if (!trace.valid)
        return false;
    position = trace.endPos;
    velocity.z -= kGravity * kTick;
    if (trace.didHit) {
        // Reflect: v -= (1 + e) * (v . n) * n
        const float dot = velocity.x * trace.normal.x + velocity.y * trace.normal.y
            + velocity.z * trace.normal.z;
        velocity.x -= (1.0f + kRestitution) * dot * trace.normal.x;
        velocity.y -= (1.0f + kRestitution) * dot * trace.normal.y;
        velocity.z -= (1.0f + kRestitution) * dot * trace.normal.z;
        outPoint = trace.endPos;
        return true;
    }
    outPoint = next;
    return false;
}

// Full throw preview: points[0] = eye; each bounce + every ~kth free-flight
// point lands in points; landing = rest position. Returns the point count.
template <typename Tracer>
inline int simulate(const cs2::Vector& eye, const cs2::Vector& forward,
                    const cs2::Vector& localVelocity, void* skipEntity,
                    Tracer&& tracer, cs2::Vector (&points)[kMaxTrajectoryPoints],
                    cs2::Vector& landing) noexcept
{
    cs2::Vector position = eye;
    cs2::Vector velocity{forward.x * kFullThrowSpeed + localVelocity.x,
                         forward.y * kFullThrowSpeed + localVelocity.y,
                         forward.z * kFullThrowSpeed + localVelocity.z};
    if (!finite(velocity) || !finite(position))
        return 0;

    int count = 0;
    points[count++] = position;
    landing = position;

    int bounces = 0;
    for (float t = 0.0f; t < kMaxSimTime; t += kTick) {
        cs2::Vector bouncePoint;
        // NOTE: the low-speed stop gate only fires GROUNDED (after a bounce).
        // The first version checked |v| every step and a free-falling grenade
        // dips under 30 u/s early - the sim stopped mid-air and the preview
        // "never landed" (caught by the straight-drop unit test).
        const bool bounced = step(position, velocity, skipEntity, bouncePoint, tracer);
        // Free flight: sample every 3rd step (~21/s, smoother arc); bounces
        // always recorded.
        const int stepIndex = static_cast<int>(t / kTick);
        if (bounced || stepIndex % 3 == 0) {
            if (count >= kMaxTrajectoryPoints)
                break;
            points[count++] = bounced ? bouncePoint : position;
        }
        if (bounced) {
            landing = bouncePoint;
            if (++bounces > kMaxBounces)
                break;
            const float speed = std::sqrt(velocity.x * velocity.x + velocity.y * velocity.y
                + velocity.z * velocity.z);
            if (speed < kStopSpeed)
                break;  // grounded and rolling slow - settled
        }
    }
    if (count > 0)
        landing = points[count - 1];
    return count;
}

}

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

        // ---- Live thrown grenades: flash timers + burning infernos ----
        const auto tickInterval = hookContext.globalVars().tickInterval().valueOr(0.015625f);
        hookContext.template make<EntitySystem>().forEachNetworkableEntityIdentity([&](const auto& entityIdentity) {
            const auto entityTypeInfo = hookContext.entityClassifier().classifyEntity(entityIdentity.entityClass);
            if (entityTypeInfo.template is<cs2::C_Inferno>()) {
                if (!GET_CONFIG_VAR(grenade_pred_vars::InfernoBounds) || scene.infernoCount >= overlay_layer::kMaxGrenadeMarkers)
                    return;
                auto&& inferno = hookContext.template make<BaseEntity>(static_cast<cs2::C_BaseEntity*>(entityIdentity.entity));
                const auto origin = inferno.absOrigin();
                if (!origin.hasValue() || !grenade_detail::finite(origin.value()))
                    return;
                // Burning gate: fire started at m_nFireEffectTickBegin (tick) and
                // burns m_nFireLifetime seconds; the entity OUTLIVES the flames,
                // so the circle only renders while curtime is inside the window.
                const auto begin = infernoInt(static_cast<cs2::C_BaseEntity*>(inferno.baseEntity()), "C_Inferno", "m_nFireEffectTickBegin");
                const auto lifetime = infernoFloat(static_cast<cs2::C_BaseEntity*>(inferno.baseEntity()), "C_Inferno", "m_nFireLifetime");
                if (!begin.hasValue() || !lifetime.hasValue())
                    return;
                const float endTime = static_cast<float>(begin.value()) * tickInterval + lifetime.value();
                if (curtime.value() >= endTime)
                    return;  // fire already out - no circle (2026-10-13 report)
                scene.infernos[scene.infernoCount++] = overlay_layer::GrenadeMarker{origin.value().x, origin.value().y, origin.value().z, 0.0f};
                return;
            }
            if (!entityTypeInfo.template is<cs2::C_FlashbangProjectile>())
                return;
            if (!GET_CONFIG_VAR(grenade_pred_vars::FlashTimer) || scene.flashCount >= overlay_layer::kMaxGrenadeMarkers)
                return;
            auto&& projectile = hookContext.template make<BaseEntity>(static_cast<cs2::C_BaseEntity*>(entityIdentity.entity));
            if (!projectile)
                return;
            const auto origin = projectile.absOrigin();
            if (!origin.hasValue() || !grenade_detail::finite(origin.value()))
                return;
            const auto spawnTime = projectileFloat(static_cast<cs2::C_BaseEntity*>(projectile.baseEntity()), "C_BaseCSGrenadeProjectile", "m_flSpawnTime");
            if (!spawnTime.hasValue())
                return;
            const float timeLeft = spawnTime.value() + 1.6f - curtime.value();
            if (timeLeft <= 0.0f)
                return;
            scene.flashes[scene.flashCount++] = overlay_layer::GrenadeMarker{origin.value().x, origin.value().y, origin.value().z, timeLeft};
        });

        // ---- Held grenade: trajectory + landing preview ----
        if (GET_CONFIG_VAR(grenade_pred_vars::Trajectory)) {
            const auto weaponType = localPawn.getActiveWeapon().weaponType();
            if (weaponType.hasValue() && weaponType.value() == 9) {  // grenade
                const auto eye = localPawn.eyePosition();
                const auto view = pawnValue<cs2::Vector>(localPawn.baseEntity(), "C_CSPlayerPawn", "m_angEyeAngles");
                const auto velocity = pawnValue<cs2::Vector>(localPawn.baseEntity(), "C_BaseEntity", "m_vecVelocity");
                if (eye.hasValue() && view.hasValue() && grenade_detail::finite(eye.value())) {
                    const auto basis = shot_geometry::angleVectors(view.value().x, view.value().y);
                    cs2::Vector localVelocity{};
                    if (velocity.hasValue())
                        localVelocity = velocity.value();
                    cs2::Vector landing{};
                    const auto tracer = [this](const cs2::Vector& from, const cs2::Vector& to, void* skip) noexcept {
                        return Tracing::traceLine(from, to, skip, Autowall::kBulletMask);
                    };
                    scene.trajectoryCount = grenade_detail::simulate(
                        eye.value(), basis.forward, localVelocity,
                        localPawn.baseEntity(), tracer, scene.trajectory, landing);
                    scene.hasLanding = scene.trajectoryCount > 0;
                    scene.landing = landing;
                }
            }
        }

        overlay_layer::publishGrenades(scene);
    }

    void onUnload() const noexcept
    {
        overlay_layer::publishGrenades({});
        wasActive = false;
    }

private:
    [[nodiscard]] Optional<int> infernoInt(cs2::C_BaseEntity* entity, const char* className, const char* fieldName) const noexcept
    {
        const auto offset = hookContext.schemaSystem().getFieldOffset(className, fieldName);
        if (!offset.has_value() || *offset <= 0)
            return {};
        int value{};
        std::memcpy(&value, reinterpret_cast<const std::byte*>(entity) + *offset, sizeof(value));
        return value;
    }

    [[nodiscard]] Optional<float> infernoFloat(cs2::C_BaseEntity* entity, const char* className, const char* fieldName) const noexcept
    {
        const auto offset = hookContext.schemaSystem().getFieldOffset(className, fieldName);
        if (!offset.has_value() || *offset <= 0)
            return {};
        float value{};
        std::memcpy(&value, reinterpret_cast<const std::byte*>(entity) + *offset, sizeof(value));
        return value;
    }

    [[nodiscard]] Optional<float> projectileFloat(cs2::C_BaseEntity* entity, const char* className, const char* fieldName) const noexcept
    {
        return infernoFloat(entity, className, fieldName);
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