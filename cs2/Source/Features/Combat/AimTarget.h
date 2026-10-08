#pragma once

#include <cmath>
#include <algorithm>
#include <array>
#include <limits>
#include <cstddef>
#include <cstring>

#include <CS2/Classes/Entities/C_BaseEntity.h>
#include <CS2/Classes/Entities/C_CSPlayerPawn.h>
#include <CS2/Classes/Vector.h>
#include <Features/Combat/TargetExtrapolator.h>
#include <Features/Combat/TargetSelection.h>
#include <Features/Combat/HitboxGeometry.h>
#include <GameClient/Entities/BaseEntity.h>
#include <GameClient/Entities/PlayerPawn.h>
#include <GameClient/EntitySystem/EntitySystem.h>
#include <Utils/Optional.h>
#include <Utils/Trig.h>

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

    struct Target {
        Angles angles;
        cs2::Vector aimPoint;
        cs2::C_BaseEntity* entity;
        int hitgroup;
        hitbox_geometry::Shape shape{};
    };

    struct HitboxFlags {
        bool head;
        bool chest;
        bool stomach;
        bool arms;
        bool legs;
    };

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
        if (!hitbox_geometry::finite(eye) || !std::isfinite(currentPitch) || !std::isfinite(currentYaw)
            || !std::isfinite(maxFov) || maxFov <= 0
            || trig::absolute(currentPitch)>36000 || trig::absolute(currentYaw)>36000)
            return bestTarget;
        struct Candidate { Target target; float score; float fov; int priority; };
        const auto better = [](const Candidate& a, const Candidate& b) {
            if (a.priority != b.priority) return a.priority < b.priority;
            if (a.score != b.score) return a.score < b.score;
            return a.fov < b.fov;
        };
        std::array<Candidate, 256> candidates;
        std::size_t candidateCount = 0;
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
            if constexpr (requires { target.hasImmunity(); }) {
                if (target.hasImmunity() != false) return;
            }
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
            Hitboxes::Set set{};
            if constexpr (requires { node.raw(); node.boneTransform(0); })
                set = Hitboxes::query(node.raw());
            // Rank real hitbox centres within each enabled group. Both arms and
            // legs participate; bone-only fallback is for unavailable model data.
            for (std::size_t i = 0; i < 5; ++i) {
                if (!enabled[i]) continue;

                const auto consider = [&](const cs2::Vector& point, const hitbox_geometry::Shape& shape) {
                    const auto offset = hitbox_geometry::subtract(point,eye);
                    const float distanceSquared = hitbox_geometry::dot(offset,offset);
                    if (!std::isfinite(distanceSquared) || distanceSquared <= 0) return;
                    const auto angles = anglesTo(eye,point);
                    const float fov = fovBetween(currentPitch,currentYaw,angles.pitch,angles.yaw);
                    if (!(fov < maxFov)) return;
                    const Target candidate{angles,point,entity,groups[i],shape};
                    const float score = mode == target_selection::Mode::Distance ? distanceSquared
                        : mode == target_selection::Mode::Health ? static_cast<float>(health.value()) : fov;
                    Candidate ranked{candidate, score, fov, static_cast<int>(i)};
                    if (candidateCount < candidates.size()) candidates[candidateCount++] = ranked;
                    else {
                        // Keep the best bounded set even on large community servers.
                        auto worst = std::max_element(candidates.begin(), candidates.end(),
                            better);
                        if (better(ranked, *worst)) *worst = ranked;
                    }
                };
                if constexpr (requires { node.raw(); node.boneTransform(0); }) {
                    for (int h=0;h<set.count;++h) {
                        const auto& entry=set.entries[h];
                        int group=Hitboxes::hitgroupFromHitbox(entry.index);
                        if (group==8) group=2;
                        if (group==5) group=4;
                        if (group==7) group=6;
                        if (group!=groups[i]) continue;
                        const auto transform=node.boneTransform(entry.bone);
                        if (!transform.hasValue()) continue;
                        auto shape=hitbox_geometry::from(entry,transform.value().position,
                                                         transform.value().rotation,transform.value().scale);
                        if (!shape.valid) continue;
                        shape.origin=hitbox_geometry::add(shape.origin,delta);
                        consider(shape.center(),shape);
                    }
                }
                if (set.count==0) {
                    const auto bone=node.bonePosition(bones[i]);
                    if (bone.hasValue()) consider(hitbox_geometry::add(bone.value(),delta),{});
                }
            }
        });
        std::sort(candidates.begin(), candidates.begin() + candidateCount, better);
        for (std::size_t i = 0; i < candidateCount; ++i) {
            if (accept(candidates[i].target)) return candidates[i].target;
        }
        return bestTarget;
    }

    [[nodiscard]] Optional<cs2::Vector> eyePosition(auto&& pawn) const noexcept
    {
        return pawn.eyePosition();
    }

private:

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

    static constexpr int kHitgroupHead = 1;
    static constexpr int kHitgroupChest = 2;
    static constexpr int kHitgroupStomach = 3;
    static constexpr int kHitgroupArm = 4;
    static constexpr int kHitgroupLeg = 6;

    HookContext& hookContext;
};
