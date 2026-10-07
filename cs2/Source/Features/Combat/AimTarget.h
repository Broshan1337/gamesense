#pragma once

#include <cmath>
#include <limits>
#include <cstddef>
#include <cstring>

#include <CS2/Classes/Entities/C_BaseEntity.h>
#include <CS2/Classes/Entities/C_CSPlayerPawn.h>
#include <CS2/Classes/Vector.h>
#include <Features/Combat/TargetExtrapolator.h>
#include <Features/Combat/TargetSelection.h>
#include <GameClient/Entities/BaseEntity.h>
#include <GameClient/Entities/PlayerPawn.h>
#include <GameClient/EntitySystem/EntitySystem.h>
#include <Utils/Optional.h>
#include <Utils/Trig.h>

// Shared target selection for the aimbots: given the local eye position, the current view angles, a
// FOV limit and which hitboxes are eligible, returns the view-relative angles of the selected enemy's
// highest-priority resolvable bone - or {} if none is within FOV. Both the silent (rage) aimbot and the
// legit aimbot use this; they differ only in how they APPLY the returned angle (the rage one writes it
// into input_history to redirect the shot silently; the legit one steps the real view toward it).
//
// The bone indices and the vector_angles/FOV math were originally proven inside the silent aimbot; they
// live here now so there is one copy. Only the head index (6) is confirmed in-game; the rest are
// best-guess CS2 player-skeleton indices - a wrong one is caught by bonePosition()'s range guard
// (skipped, never a crash), it just aims at the wrong spot until corrected.
template <typename HookContext>
class AimTarget {
public:
    explicit AimTarget(HookContext& hookContext) noexcept
        : hookContext{hookContext}
    {
    }

    struct Angles {
        float pitch;
        float yaw;
    };

    // The chosen target: the view-relative angles to aim, plus the world-space aim point (the possibly
    // extrapolated bone) and the enemy pawn, so callers can gate on hitchance / min-damage to that exact
    // point. The legit aimbot uses only `angles`.
    struct Target {
        Angles angles;
        cs2::Vector aimPoint;
        cs2::C_BaseEntity* entity;
        int hitgroup; // CS2 hitgroup of the aimed bone (1=head, 2=chest, 3=stomach, 4=arm, 6=leg) - for damage scaling
    };

    // Which body parts are eligible, tried in the order Head > Chest > Stomach > Arms > Legs.
    struct HitboxFlags {
        bool head;
        bool chest;
        bool stomach;
        bool arms;
        bool legs;
    };

    // Select eligible points after applying the caller's acceptance predicate.
    [[nodiscard]] Optional<Target> best(const cs2::Vector& eye, float currentPitch, float currentYaw, float maxFov, const HitboxFlags& hitboxes, int extrapolateTicks = 0, const cs2::C_BaseEntity* excludedEntity = nullptr, const cs2::C_BaseEntity* preferredEntity = nullptr) const noexcept
    {
        return bestPassing(eye, currentPitch, currentYaw, maxFov, hitboxes,
            [](const Target&) { return true; }, extrapolateTicks, excludedEntity, preferredEntity);
    }

    // Keep a valid incumbent when requested; otherwise rank eligible enemies by mode.
    template <typename Accept>
    [[nodiscard]] Optional<Target> acquire(const cs2::Vector& eye, float pitch, float yaw, float maxFov,
                                          const HitboxFlags& hitboxes, Accept&& accept,
                                          const cs2::C_BaseEntity* preferredEntity = nullptr,
                                          target_selection::Mode mode = target_selection::Mode::Crosshair) const noexcept
    {
        if (preferredEntity) {
            auto incumbent = bestPassing(eye, pitch, yaw, maxFov, hitboxes, accept, 0, nullptr, preferredEntity, mode);
            if (incumbent.hasValue())
                return incumbent;
        }
        return bestPassing(eye, pitch, yaw, maxFov, hitboxes, accept, 0, preferredEntity, nullptr, mode);
    }

    template <typename Accept>
    [[nodiscard]] Optional<Target> bestPassing(const cs2::Vector& eye, float currentPitch, float currentYaw,
                                               float maxFov, const HitboxFlags& hitboxes, Accept&& accept,
                                               int extrapolateTicks = 0, const cs2::C_BaseEntity* excludedEntity = nullptr,
                                               const cs2::C_BaseEntity* preferredEntity = nullptr,
                                               target_selection::Mode mode = target_selection::Mode::Crosshair) const noexcept
    {
        Optional<Target> bestTarget;
        float bestFov = maxFov;
        float bestScore = std::numeric_limits<float>::infinity();
        hookContext.template make<EntitySystem>().forEachNetworkableEntityIdentity([&](const auto& identity) {
            auto* const entity = static_cast<cs2::C_BaseEntity*>(identity.entity);
            if (entity == excludedEntity || (preferredEntity && entity != preferredEntity))
                return;
            auto&& baseEntity = hookContext.template make<BaseEntity>(entity);
            if (!baseEntity.classify().template is<cs2::C_CSPlayerPawn>())
                return;
            auto&& target = baseEntity.template as<PlayerPawn>();
            if (!target || target.isControlledByLocalPlayer() || target.isEnemy() != true || target.isAlive() != true)
                return;
            const auto health = target.health();
            if (!health.hasValue() || health.value() <= 0)
                return;

            auto&& node = target.baseEntity().gameSceneNode();
            const auto delta = extrapolateTicks > 0
                ? hookContext.template make<TargetExtrapolator>().predictedDelta(target, extrapolateTicks)
                : cs2::Vector{};
            const bool enabled[]{hitboxes.head, hitboxes.chest, hitboxes.stomach, hitboxes.arms, hitboxes.legs};
            constexpr int bones[]{kHeadBone, kChestBone, kStomachBone, kArmsBone, kLegsBone};
            constexpr int groups[]{kHitgroupHead, kHitgroupChest, kHitgroupStomach, kHitgroupArm, kHitgroupLeg};
            // Preserve hitbox priority, falling back when a point is blocked or outside the FOV.
            for (std::size_t i = 0; i < 5; ++i) {
                if (!enabled[i])
                    continue;
                const auto bone = node.bonePosition(bones[i]);
                if (!bone.hasValue())
                    continue;
                const cs2::Vector point{bone.value().x + delta.x, bone.value().y + delta.y, bone.value().z + delta.z};
                const float dx = point.x - eye.x;
                const float dy = point.y - eye.y;
                const float dz = point.z - eye.z;
                const float distanceSquared = dx * dx + dy * dy + dz * dz;
                if (!std::isfinite(distanceSquared) || distanceSquared <= 0.0f)
                    continue;
                const auto needed = anglesTo(eye, point);
                const float fov = fovBetween(currentPitch, currentYaw, needed.pitch, needed.yaw);
                if (!(fov < maxFov))
                    continue;
                const Target candidate{needed, point, entity, groups[i]};
                if (!accept(candidate))
                    continue;
                const float score = mode == target_selection::Mode::Distance ? distanceSquared
                    : mode == target_selection::Mode::Health ? static_cast<float>(health.value()) : fov;
                if (score < bestScore || (score == bestScore && fov < bestFov)) {
                    bestScore = score;
                    bestFov = fov;
                    bestTarget = candidate;
                }
                break;
            }
        });
        return bestTarget;
    }

    // Eye position of the given pawn (origin + m_vecViewOffset). Thin pass-through to
    // PlayerPawn::eyePosition so there is one implementation; kept here so callers that already hold an
    // AimTarget do not need a separate handle.
    [[nodiscard]] Optional<cs2::Vector> eyePosition(auto&& pawn) const noexcept
    {
        return pawn.eyePosition();
    }

private:
    // Pitch/yaw (degrees) that point from `from` to `to`. Matches the engine's own vector_angles:
    // yaw = atan2(dy, dx), pitch = atan2(-dz, hypot(dx, dy)). Uses the project's libm-free trig.
    [[nodiscard]] static Angles anglesTo(const cs2::Vector& from, const cs2::Vector& to) noexcept
    {
        const auto dx = to.x - from.x;
        const auto dy = to.y - from.y;
        const auto dz = to.z - from.z;

        const auto horizontal = trig::squareRoot(dx * dx + dy * dy);
        const auto yaw = trig::arcTangent2(dy, dx) * trig::kRadiansToDegrees;
        const auto pitch = trig::arcTangent2(-dz, horizontal) * trig::kRadiansToDegrees;
        return Angles{pitch, yaw};
    }

    // Angular distance between two pitch/yaw pairs, in degrees, with the yaw difference wrapped to
    // (-180, 180] so crossing the 180/-180 seam is not counted as a huge move.
    [[nodiscard]] static float fovBetween(float pitch0, float yaw0, float pitch1, float yaw1) noexcept
    {
        const auto dPitch = pitch1 - pitch0;
        const auto dYaw = trig::normalizeDegrees(yaw1 - yaw0);
        return trig::squareRoot(dPitch * dPitch + dYaw * dYaw);
    }

    static constexpr int kHeadBone = 6;
    static constexpr int kChestBone = 4;
    static constexpr int kStomachBone = 2;
    static constexpr int kArmsBone = 9;
    static constexpr int kLegsBone = 25;

    // CS2 hitgroup ids for the aimed bone, used for damage scaling (headshot multiplier, leg 0.75x, etc.).
    static constexpr int kHitgroupHead = 1;
    static constexpr int kHitgroupChest = 2;
    static constexpr int kHitgroupStomach = 3;
    static constexpr int kHitgroupArm = 4;
    static constexpr int kHitgroupLeg = 6;

    HookContext& hookContext;
};
