#pragma once

#include <cstddef>
#include <cstring>

#include <CS2/Classes/Entities/C_BaseEntity.h>
#include <CS2/Classes/Entities/C_CSPlayerPawn.h>
#include <CS2/Classes/Vector.h>
#include <Features/Combat/TargetExtrapolator.h>
#include <GameClient/Entities/BaseEntity.h>
#include <GameClient/Entities/PlayerPawn.h>
#include <GameClient/EntitySystem/EntitySystem.h>
#include <Utils/Optional.h>
#include <Utils/Trig.h>

// Shared target selection for the aimbots: given the local eye position, the current view angles, a
// FOV limit and which hitboxes are eligible, returns the view-relative angles of the lowest-FOV enemy's
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

    // The angles of the best (lowest-FOV) eligible enemy's target hitbox, relative to the current view,
    // or {} if none is within the FOV limit.
    // `extrapolateTicks` > 0 leads each target: its aim point is shifted by TargetExtrapolator's predicted
    // origin delta over that many ticks (0 = aim at the current position, the previous behavior).
    // `excludedEntity` skips one entity - the visibility-aware selection in Aimbot uses it to fall
    // through rejected candidates to the next-best hittable one.
    // `preferredEntity` INVERTS the selection: when set, ONLY that entity is considered (all the usual
    // validity checks still apply). This is the aimbot's target stickiness - re-selecting the FOV-best
    // target every tick among many candidates flips the aim between them, measured in-game as the
    // camera swinging left/right while firing with autoshoot.
    [[nodiscard]] Optional<Target> best(const cs2::Vector& eye, float currentPitch, float currentYaw, float maxFov, const HitboxFlags& hitboxes, int extrapolateTicks = 0, const cs2::C_BaseEntity* excludedEntity = nullptr, const cs2::C_BaseEntity* preferredEntity = nullptr) const noexcept
    {
        Optional<Target> bestTarget;
        float bestFov = maxFov;

        hookContext.template make<EntitySystem>().forEachNetworkableEntityIdentity([&](const auto& identity) {
            if (excludedEntity && static_cast<const cs2::C_BaseEntity*>(identity.entity) == excludedEntity)
                return;
            if (preferredEntity && static_cast<const cs2::C_BaseEntity*>(identity.entity) != preferredEntity)
                return;
            auto&& baseEntity = hookContext.template make<BaseEntity>(static_cast<cs2::C_BaseEntity*>(identity.entity));
            if (!baseEntity.classify().template is<cs2::C_CSPlayerPawn>())
                return;

            auto&& target = baseEntity.template as<PlayerPawn>();
            if (!target || target.isControlledByLocalPlayer())
                return;
            if (target.isEnemy() != true || target.isAlive() != true)
                return;
            if (const auto health = target.health(); !health.hasValue() || health.value() <= 0)
                return;

            int hitgroup = 0;
            const auto bone = targetBonePosition(target, hitboxes, hitgroup);
            if (!bone.hasValue())
                return;

            cs2::Vector aimPoint = bone.value();
            if (extrapolateTicks > 0) {
                const auto delta = hookContext.template make<TargetExtrapolator>().predictedDelta(target, extrapolateTicks);
                aimPoint.x += delta.x;
                aimPoint.y += delta.y;
                aimPoint.z += delta.z;
            }

            const auto needed = anglesTo(eye, aimPoint);
            const auto fov = fovBetween(currentPitch, currentYaw, needed.pitch, needed.yaw);
            if (fov < bestFov) {
                bestFov = fov;
                bestTarget = Target{needed, aimPoint, static_cast<cs2::C_BaseEntity*>(target.baseEntity()), hitgroup};
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
    // World position of the highest-priority ENABLED hitbox that resolves on this target, in the order
    // Head > Chest > Stomach > Arms > Legs. bonePosition() returns {} for an out-of-range or implausible
    // index, so an unenabled part - or a wrong bone index - is simply skipped and the next priority is
    // tried.
    [[nodiscard]] Optional<cs2::Vector> targetBonePosition(auto&& target, const HitboxFlags& hitboxes, int& hitgroupOut) const noexcept
    {
        auto&& node = target.baseEntity().gameSceneNode();

        if (hitboxes.head)
            if (const auto bone = node.bonePosition(kHeadBone); bone.hasValue()) {
                hitgroupOut = kHitgroupHead;
                return bone;
            }
        if (hitboxes.chest)
            if (const auto bone = node.bonePosition(kChestBone); bone.hasValue()) {
                hitgroupOut = kHitgroupChest;
                return bone;
            }
        if (hitboxes.stomach)
            if (const auto bone = node.bonePosition(kStomachBone); bone.hasValue()) {
                hitgroupOut = kHitgroupStomach;
                return bone;
            }
        if (hitboxes.arms)
            if (const auto bone = node.bonePosition(kArmsBone); bone.hasValue()) {
                hitgroupOut = kHitgroupArm;
                return bone;
            }
        if (hitboxes.legs)
            if (const auto bone = node.bonePosition(kLegsBone); bone.hasValue()) {
                hitgroupOut = kHitgroupLeg;
                return bone;
            }
        return {};
    }

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
