#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include <CS2/Classes/CUserCmd.h>
#include <CS2/Classes/Entities/C_BaseEntity.h>
#include <CS2/Classes/Entities/C_CSPlayerPawn.h>
#include <CS2/Classes/Vector.h>
#include <Features/Combat/AimTarget.h>
#include <Features/Combat/AttackCommand.h>
#include <Features/Combat/Autowall/Autowall.h>
#include <Features/Combat/ShotGeometry.h>
#include <Features/Combat/SubtickShotWriter.h>
#include <Features/Game/FvaEmulator.h>
#include <GameClient/Hitboxes.h>
#include <GameClient/Lagcomp.h>
#include <GameClient/MultiPoint.h>
#include <Features/Combat/Aimbot/AimbotConfigVariables.h>
#include <GameClient/Entities/BaseEntity.h>
#include <GameClient/Entities/PlayerPawn.h>
#include <GameClient/EntitySystem/EntitySystem.h>
#include <GameClient/MouseState.h>
#include <GameClient/SubtickMoves.h>
#include <GameClient/SpreadPrediction/SpreadSolver.h>
#include <GameClient/UserCmd.h>
#include <HookContext/HookContextMacros.h>
#include <SDL/SdlFunctions.h>
#include <Utils/FastMath.h>
#include <Utils/Optional.h>
#include <Utils/Trig.h>
#include <Utils/VerifyConsole.h>
















template <typename HookContext>
class Aimbot {
public:
    explicit Aimbot(HookContext& hookContext) noexcept
        : hookContext{hookContext}
    {
    }

    void onCreateMove(cs2::CUserCmd* cmd) const noexcept
    {
        
        forceShotThisTick = false;

        if (!GET_CONFIG_VAR(aimbot_vars::Enabled)) {
            lastTargetHandleValue = 0;
            return;
        }

        
        
        
        
        
        
        
        
        
        
        
        const bool backtrackEnabled = GET_CONFIG_VAR(aimbot_vars::Backtrack);
        const bool extrapolateEnabled = GET_CONFIG_VAR(aimbot_vars::Extrapolate);

        const bool forceShotEnabled = GET_CONFIG_VAR(aimbot_vars::ForceShot) || GET_CONFIG_VAR(aimbot_vars::ForceShotAir);
        const bool userFiring = MouseState::isButtonDown(sdl3::mousebutton::kLeft);
        if (!userFiring && !forceShotEnabled) {
            lastTargetHandleValue = 0;
            return;
        }

        UserCmd userCmd{cmd};
        if (!userCmd) {
            lastTargetHandleValue = 0;
            return;
        }

        auto&& localPawn = hookContext.activeLocalPlayerPawn();
        if (!localPawn || localPawn.isAlive() != true) {
            lastTargetHandleValue = 0;
            return;
        }

        auto aimTarget = hookContext.template make<AimTarget>();
        const auto eye = aimTarget.eyePosition(localPawn);
        const auto currentPitch = userCmd.viewPitch();
        const auto currentYaw = userCmd.viewYaw();
        if (!eye.hasValue() || !currentPitch.hasValue() || !currentYaw.hasValue()) {
            lastTargetHandleValue = 0;
            return;
        }

        
        
        
        float maxFov = aimbot_params::kMaxFov;
        if (GET_CONFIG_VAR(aimbot_vars::SpreadCircleFov)) {
            if (const auto cone = hookContext.localPlayerBulletInaccuracy(); cone.hasValue() && cone.value() > 0.0f)
                maxFov = trig::arcTangent2(cone.value(), 1.0f) * trig::kRadiansToDegrees;
        }
        
        
        
        
        
        
        
        cs2::C_BaseEntity* preferredTarget = nullptr;
        if (GET_CONFIG_VAR(aimbot_vars::TargetLock) && lastTargetHandleValue != 0) {
            if (auto* const instance = hookContext.template make<EntitySystem>().getEntityFromHandle(cs2::CEntityHandle{lastTargetHandleValue}))
                preferredTarget = static_cast<cs2::C_BaseEntity*>(instance);
        }

        const auto aim = aimTarget.acquire(eye.value(), currentPitch.value(), currentYaw.value(), maxFov,
            hitboxFlags(), [&](const auto& candidate) { return passesVisibility(localPawn, eye.value(), candidate); },
            preferredTarget, static_cast<target_selection::Mode>(static_cast<std::uint8_t>(GET_CONFIG_VAR(aimbot_vars::TargetSelection))));
        if (!aim.hasValue()) {
            lastTargetHandleValue = 0;
            return;
        }
        lastTargetHandleValue = hookContext.template make<BaseEntity>(aim.value().entity).handle().value;

        auto chosen = aim.value();
        if (GET_CONFIG_VAR(aimbot_vars::Multipoint))
            chosen = refineMultipoint(localPawn, eye.value(), chosen);

        
        
        if (!passesVisibility(localPawn, eye.value(), chosen))
            return; 

        
        
        
        
        float backtrackSimTime = 0.0f;
        if (backtrackEnabled || extrapolateEnabled) {
            const auto flags = hitboxFlags();
            auto&& lagcomp = hookContext.template make<Lagcomp>();
            Optional<typename Lagcomp<HookContext>::Result> picked;
            if (backtrackEnabled) {
                
                
                
                
                picked = bestRecordPoint(localPawn, eye.value(), currentPitch.value(), currentYaw.value(), chosen.entity, flags, maxFov, static_cast<int>(GET_CONFIG_VAR(aimbot_vars::BacktrackTicks)));
                if (!picked.hasValue())
                    picked = lagcomp.bestRecord(chosen.entity, eye.value(), chosen.angles.pitch, chosen.angles.yaw, flags.head, flags.chest, flags.stomach, flags.arms, flags.legs, static_cast<int>(GET_CONFIG_VAR(aimbot_vars::BacktrackTicks)));
            }
            if (!picked.hasValue() && extrapolateEnabled) {
                
                
                auto extrapolated = lagcomp.extrapolate(chosen.entity, static_cast<int>(GET_CONFIG_VAR(aimbot_vars::ExtrapolateTicks)));
                if (extrapolated.hasValue()) {
                    auto&& extrapNode = hookContext.template make<BaseEntity>(chosen.entity).gameSceneNode();
                    if (extrapNode) {
                        const auto hitboxSet = Hitboxes::query(extrapNode.raw());
                        float targetHealth = 100.0f;
                        auto&& targetPawn = hookContext.template make<BaseEntity>(chosen.entity).template as<PlayerPawn>();
                        if (targetPawn)
                            if (const auto hp = targetPawn.health(); hp.hasValue())
                                targetHealth = static_cast<float>(hp.value());
                        int budget = kMaxScanPoints;
                        auto scored = bestPointOnSkeleton(localPawn, eye.value(), currentPitch.value(), currentYaw.value(), chosen.entity, flags, maxFov, hitboxSet, extrapolated.value(), 0, targetHealth, budget);
                        if (scored.hasValue())
                            picked = scored.value().result;
                    }
                }
                if (!picked.hasValue()) {
                    picked = lagcomp.extrapolatedResult(chosen.entity, eye.value(), chosen.angles.pitch, chosen.angles.yaw, flags.head, flags.chest, flags.stomach, flags.arms, flags.legs, static_cast<int>(GET_CONFIG_VAR(aimbot_vars::ExtrapolateTicks)));
                }
            }
            if (picked.hasValue()) {
                const auto angles = shot_geometry::anglesTo(eye.value(), picked.value().aimPoint);
                chosen.angles = typename AimTarget<HookContext>::Angles{angles.pitch, angles.yaw};
                chosen.aimPoint = picked.value().aimPoint;
                chosen.hitgroup = picked.value().hitgroup;

                
                
                
                
                
                
                
                const auto serverTick = hookContext.localPlayerController().tickBase();
                const int recordTick = static_cast<int>(picked.value().simulationTime / Lagcomp<HookContext>::kTickInterval);
                if (!serverTick.hasValue() || recordTick < serverTick.value())
                    backtrackSimTime = picked.value().simulationTime;
            }
        }

        
        
        // Historical and extrapolated points need their own visibility gate.
        if (!passesVisibility(localPawn, eye.value(), chosen))
            return;

        if (GET_CONFIG_VAR(aimbot_vars::AutoStop))
            autoStop(localPawn, currentYaw.value(), userCmd);

        
        
        
        
        float punchPitch = 0.0f, punchYaw = 0.0f;
        if (GET_CONFIG_VAR(aimbot_vars::RecoilCompensation)) {
            if (const auto punch = localPawn.aimPunchAngle(); punch.hasValue()) {
                punchPitch = punch.value().x;
                punchYaw = punch.value().y;
            }
        }

        
        
        
        
        
        
        
        
        
        
        
        
        
        
        
        stagedCorrectionValid = false;
        if (GET_CONFIG_VAR(aimbot_vars::SpreadCompensation)) {
            auto solver = hookContext.template make<SpreadSolver>();
            const auto tick = hookContext.localPlayerController().tickBase();
            if (tick.hasValue() && tick.value() > 0) {
                if (const auto params = solver.weaponParams(localPawn.getActiveWeapon()); params.hasValue()) {
                    if (const auto corrected = solver.findSpreadCorrection(typename SpreadSolver<HookContext>::Angles{chosen.angles.pitch, chosen.angles.yaw, 0.0f}, tick.value(), params.value()); corrected.hasValue()) {
                        stagedCorrectionAngles = corrected.value();
                        stagedCorrectionValid = true;
                    }
                }
            }
        }

        stagedAimPitch = chosen.angles.pitch;
        stagedAimYaw = chosen.angles.yaw;
        stagedPunchPitch = punchPitch;
        stagedPunchYaw = punchYaw;
        stagedBacktrackSimTime = backtrackSimTime;
        stagedSpreadCompensation = GET_CONFIG_VAR(aimbot_vars::SpreadCompensation);
        stagedEye = eye.value();
        stagedAimPoint = chosen.aimPoint;
        shotStagedThisTick = true;
        lastTargetHandleValue = hookContext.template make<BaseEntity>(chosen.entity).handle().value;

        
        
        
        
        
        
        if (stagedCorrectionValid)
            fva::setTargetAngle(stagedCorrectionAngles.pitch, stagedCorrectionAngles.yaw);
        else
            fva::setTargetAngle(chosen.angles.pitch, chosen.angles.yaw);

        
        
        
        
        
        
        
        
        
        
        bool forceDown = false;
        if (forceShotEnabled) {
            if (shouldForceShoot(localPawn, eye.value(), chosen)) {
                waitArmed = false;
                forceDown = true;
            } else if (GET_CONFIG_VAR(aimbot_vars::ForceShotWait)) {
                const auto tick = hookContext.localPlayerController().tickBase();
                if (!tick.hasValue() || tick.value() <= 0) {
                    waitArmed = false;
                } else {
                    if (!waitArmed || tick.value() < waitArmedAtTick) {
                        waitArmed = true;
                        waitArmedAtTick = tick.value();
                        waitFireAtTick = tick.value() + static_cast<int>(GET_CONFIG_VAR(aimbot_vars::ForceShotWaitTicks));
                    }
                    if (tick.value() >= waitFireAtTick)
                        waitArmed = false; 
                }
            } else {
                waitArmed = false;
            }
        }
        forceShotThisTick = forceDown;
    }

    
    
    
    
    
    
    
    
    
    
    
    void onWriteMoveCrc(cs2::CUserCmd* cmd) const noexcept
    {
        bool gateHeldFire = false;
        if (shotStagedThisTick) {
            shotStagedThisTick = false;
            auto&& localPawn = hookContext.activeLocalPlayerPawn();
            if (localPawn) {
                int redirectedEntry = -1;
                const bool backtrackShot = stagedBacktrackSimTime > 0.0f;
                const typename SpreadSolver<HookContext>::Angles* precomputed =
                    (!backtrackShot && stagedCorrectionValid) ? &stagedCorrectionAngles : nullptr;
                const bool lands = hookContext.template make<SubtickShotWriter>().run(cmd, localPawn, stagedAimPitch, stagedAimYaw,
                                                                                      stagedPunchPitch, stagedPunchYaw,
                                                                                      stagedBacktrackSimTime, stagedSpreadCompensation,
                                                                                      &redirectedEntry, precomputed);
                
                
                
                
                
                
                
                
                
                
                
                
                
                
                
                
                const bool seedLucky = !lands
                    && GET_CONFIG_VAR(aimbot_vars::SeedFallback)
                    && stagedBacktrackSimTime <= 0.0f
                    && seedLandsOnTarget(localPawn);

                if (!lands && !seedLucky && GET_CONFIG_VAR(aimbot_vars::SpreadGate)) {
                    gateHeldFire = true;
                    const UserCmd userCmd{cmd};
                    userCmd.suppressAttack(cs2::CCSGOInput::Buttons::kAttack);
                    SubtickMoves<HookContext>::releaseButton(userCmd.baseMessage(), cs2::CCSGOInput::Buttons::kAttack);
                    
                    
                    
                    auto solver = hookContext.template make<SpreadSolver>();
                    const auto diagParams = solver.weaponParams(localPawn.getActiveWeapon());
                    const auto diagTick = hookContext.localPlayerController().tickBase();
                    if (diagParams.hasValue())
                        VerifyConsole::write(1.0f, "gate", "holding fire: no correction (inac=%.6f spr=%.6f recoil=%.1f tick=%d aim=(%.1f, %.1f))",
                                             diagParams.value().inaccuracy, diagParams.value().spread,
                                             diagParams.value().recoilIndex, diagTick.valueOr(-1),
                                             stagedAimPitch, stagedAimYaw);
                    else
                        VerifyConsole::write(1.0f, "gate", "holding fire: weapon spread state unresolvable");
                } else if (redirectedEntry >= 0) {
                    
                    
                    
                    
                    
                    
                    const UserCmd userCmd{cmd};
                    if (userCmd.forceAttack1StartHistoryIndex(redirectedEntry))
                        
                        
                        
                        VerifyConsole::write(0.25f, "mark", "attack marker -> entry %d", redirectedEntry);
                }
            }
        }

        if (!forceShotThisTick || gateHeldFire) {
            forceShotThisTick = false;
            return;
        }
        forceShotThisTick = false;
        hookContext.template make<AttackCommand>().press(cmd);
    }

    
    
    
    
    
    
    [[nodiscard]] bool seedLandsOnTarget(auto&& localPawn) const noexcept
    {
        const float writtenPitch = stagedAimPitch - stagedPunchPitch;
        const float writtenYaw = stagedAimYaw - stagedPunchYaw;

        auto solver = hookContext.template make<SpreadSolver>();
        const auto params = solver.weaponParams(localPawn.getActiveWeapon());
        const auto tick = hookContext.localPlayerController().tickBase();
        if (!params.hasValue() || !tick.hasValue())
            return false;

        const auto seed = solver.seed(typename SpreadSolver<HookContext>::Angles{writtenPitch, writtenYaw, 0.0f}, tick.value());
        if (!seed.hasValue())
            return false;
        const auto offset = solver.spreadOffset(seed.value(), params.value());

        const auto basis = shot_geometry::angleVectors(writtenPitch, writtenYaw);
        const cs2::Vector direction = shot_geometry::normalized(cs2::Vector{
            basis.forward.x + basis.left.x * offset.x + basis.up.x * offset.y,
            basis.forward.y + basis.left.y * offset.x + basis.up.y * offset.y,
            basis.forward.z + basis.left.z * offset.x + basis.up.z * offset.y,
        });
        return shot_geometry::rayReachesSphere(stagedEye, direction, stagedAimPoint, kSeedFallbackRadius);
    }

    
    
    void onUnload() const noexcept
    {
        forceShotThisTick = false;
        shotStagedThisTick = false;
        lastTargetHandleValue = 0;
        waitArmed = false;
        waitArmedAtTick = -1;
        waitFireAtTick = -1;
    }

private:
    
    
    
    
    
    [[nodiscard]] bool shouldForceShoot(auto&& localPawn, const cs2::Vector& eye, const typename AimTarget<HookContext>::Target& target) const noexcept
    {
        const bool ground = GET_CONFIG_VAR(aimbot_vars::ForceShot);
        const bool air = GET_CONFIG_VAR(aimbot_vars::ForceShotAir);
        const auto onGround = localPawn.isOnGround();
        if (!onGround.hasValue())
            return false;
        if (!(onGround.value() ? ground : air))
            return false;

        
        
        const int minDamage = GET_CONFIG_VAR(aimbot_vars::MinDamage);
        if (minDamage > 0 && estimatedDamage(localPawn, eye, target) < static_cast<float>(minDamage))
            return false;

        if (localPawn.isAtMaxAccuracy())
            return true;

        const int hitchance = GET_CONFIG_VAR(aimbot_vars::Hitchance);
        return hitchance > 0 && passesHitchance(localPawn, eye, target, hitchance);
    }

    
    
    
    
    
    [[nodiscard]] bool passesHitchance(auto&& localPawn, const cs2::Vector& eye, const typename AimTarget<HookContext>::Target& target, int thresholdPercent) const noexcept
    {
        const int fraction = hitchanceFraction(localPawn, eye, target, kHitchanceSamples);
        return fraction >= 0 && fraction >= thresholdPercent;
    }

    
    
    
    [[nodiscard]] int hitchanceFraction(auto&& localPawn, const cs2::Vector& eye, const typename AimTarget<HookContext>::Target& target, int samples) const noexcept
    {
        auto solver = hookContext.template make<SpreadSolver>();
        const auto params = solver.weaponParams(localPawn.getActiveWeapon());
        if (!params.hasValue() || samples <= 0)
            return -1;

        const auto basis = shot_geometry::angleVectors(target.angles.pitch, target.angles.yaw);

        int hits = 0;
        for (int sample = 0; sample < samples; ++sample) {
            const auto spread = solver.spreadOffset(static_cast<std::uint32_t>(sample), params.value());
            const cs2::Vector direction = shot_geometry::normalized(cs2::Vector{
                basis.forward.x + basis.left.x * spread.x + basis.up.x * spread.y,
                basis.forward.y + basis.left.y * spread.x + basis.up.y * spread.y,
                basis.forward.z + basis.left.z * spread.x + basis.up.z * spread.y,
            });
            if (shot_geometry::rayReachesSphere(eye, direction, target.aimPoint, kHitchanceRadius))
                ++hits;
        }
        return hits * 100 / samples;
    }

    
    
    
    
    
    
    
    
    
    
    static constexpr int kMultipointStickyBonus = 6;

    [[nodiscard]] typename AimTarget<HookContext>::Target refineMultipoint(auto&& localPawn, const cs2::Vector& eye, const typename AimTarget<HookContext>::Target& target) const noexcept
    {
        
        
        
        if (auto capsule = refineMultipointCapsule(localPawn, eye, target); capsule.hasValue())
            return capsule.value();

        const float radius = (target.hitgroup == 1) ? kMultipointHeadRadius : kMultipointBodyRadius;
        const auto basis = shot_geometry::angleVectors(target.angles.pitch, target.angles.yaw);

        
        
        const bool incumbent = stickyMultipointEntity == target.entity && stickyMultipointHitgroup == target.hitgroup;

        auto best = target;
        int bestSlot = 0; 
        int bestFraction = hitchanceFraction(localPawn, eye, target, kMultipointSamples);
        if (incumbent && stickyMultipointSlot == bestSlot)
            bestFraction += kMultipointStickyBonus;

        
        const float offsets[4][2] = {{radius, 0.0f}, {-radius, 0.0f}, {0.0f, radius}, {0.0f, -radius}};
        for (int i = 0; i < 4; ++i) {
            const auto& offset = offsets[i];
            const cs2::Vector point{
                target.aimPoint.x + basis.left.x * offset[0] + basis.up.x * offset[1],
                target.aimPoint.y + basis.left.y * offset[0] + basis.up.y * offset[1],
                target.aimPoint.z + basis.left.z * offset[0] + basis.up.z * offset[1],
            };
            const auto angles = shot_geometry::anglesTo(eye, point);
            const typename AimTarget<HookContext>::Target candidate{{angles.pitch, angles.yaw}, point, target.entity, target.hitgroup};
            if (!passesVisibility(localPawn, eye, candidate))
                continue;
            int fraction = hitchanceFraction(localPawn, eye, candidate, kMultipointSamples);
            if (incumbent && stickyMultipointSlot == i + 1)
                fraction += kMultipointStickyBonus;
            if (fraction > bestFraction) {
                bestFraction = fraction;
                best = candidate;
                bestSlot = i + 1;
            }
        }

        
        
        
        stickyMultipointEntity = target.entity;
        stickyMultipointHitgroup = target.hitgroup;
        stickyMultipointSlot = bestSlot;
        return best;
    }

    
    
    
    
    
    [[nodiscard]] Optional<typename AimTarget<HookContext>::Target> refineMultipointCapsule(auto&& localPawn, const cs2::Vector& eye, const typename AimTarget<HookContext>::Target& target) const noexcept
    {
        auto&& node = hookContext.template make<BaseEntity>(target.entity).gameSceneNode();
        if (!node)
            return {};
        const auto hitboxSet = Hitboxes::query(node.raw());
        if (hitboxSet.count == 0)
            return {};

        
        
        
        const Hitboxes::Entry* aimed = nullptr;
        float bestDistSq = 1.0e9f;
        for (int h = 0; h < hitboxSet.count; ++h) {
            const auto& entry = hitboxSet.entries[h];
            if (canonicalHitgroup(Hitboxes::hitgroupFromHitbox(entry.index)) != target.hitgroup)
                continue;
            const auto centre = node.boneTransform(entry.bone);
            if (!centre.hasValue())
                continue;
            const auto& p = centre.value().position;
            const float dx = p.x - target.aimPoint.x;
            const float dy = p.y - target.aimPoint.y;
            const float dz = p.z - target.aimPoint.z;
            const float distSq = dx * dx + dy * dy + dz * dz;
            if (distSq < bestDistSq) {
                bestDistSq = distSq;
                aimed = &entry;
            }
        }
        if (!aimed)
            return {};

        const auto bone = node.boneTransform(aimed->bone);
        if (!bone.hasValue())
            return {};

        MultiPoint::Point points[MultiPoint::kMaxPoints];
        const int pointCount = MultiPoint::generate(*aimed, bone.value().position, bone.value().rotation,
                                                    static_cast<float>(GET_CONFIG_VAR(aimbot_vars::PointScale)), eye,
                                                    hookContext.localPlayerBulletInaccuracy().valueOr(0.0f),
                                                    GET_CONFIG_VAR(aimbot_vars::DynamicPointscale), points);
        if (pointCount <= 0)
            return {};

        const bool incumbent = stickyMultipointEntity == target.entity && stickyMultipointHitgroup == target.hitgroup;

        auto best = target;
        int bestSlot = 0; 
        int bestFraction = hitchanceFraction(localPawn, eye, target, kMultipointSamples);
        if (incumbent && stickyMultipointSlot == bestSlot)
            bestFraction += kMultipointStickyBonus;

        for (int i = 0; i < pointCount; ++i) {
            const auto& point = points[i].position;
            const auto angles = shot_geometry::anglesTo(eye, point);
            const typename AimTarget<HookContext>::Target candidate{{angles.pitch, angles.yaw}, point, target.entity, target.hitgroup};
            if (!passesVisibility(localPawn, eye, candidate))
                continue;
            int fraction = hitchanceFraction(localPawn, eye, candidate, kMultipointSamples);
            if (incumbent && stickyMultipointSlot == i + 1)
                fraction += kMultipointStickyBonus;
            if (fraction > bestFraction) {
                bestFraction = fraction;
                best = candidate;
                bestSlot = i + 1;
            }
        }

        stickyMultipointEntity = target.entity;
        stickyMultipointHitgroup = target.hitgroup;
        stickyMultipointSlot = bestSlot;
        return best;
    }

    
    
    
    
    
    struct ScoredAim {
        typename Lagcomp<HookContext>::Result result;
        float score;
    };

    [[nodiscard]] static bool hitgroupAllowed(int canonicalHitgroupId, const typename AimTarget<HookContext>::HitboxFlags& flags) noexcept
    {
        switch (canonicalHitgroupId) {
        case 1: return flags.head;
        case 2: return flags.chest;
        case 3: return flags.stomach;
        case 4: return flags.arms;
        case 6: return flags.legs;
        default: return false;
        }
    }

    
    
    [[nodiscard]] static int canonicalHitgroup(int hitgroup) noexcept
    {
        switch (hitgroup) {
        case 8: return 2;
        case 7: return 6;
        case 5: return 4;
        default: return hitgroup;
        }
    }

    
    
    [[nodiscard]] Optional<ScoredAim> bestPointOnSkeleton(auto&& localPawn, const cs2::Vector& eye, float pitch, float yaw,
                                                          cs2::C_BaseEntity* entity,
                                                          const typename AimTarget<HookContext>::HitboxFlags& flags,
                                                          float maxFov, const Hitboxes::Set& hitboxSet,
                                                          const typename Lagcomp<HookContext>::Record& record,
                                                          int ageTicks, float targetHealth, int& evaluationsLeft) const noexcept
    {
        Optional<ScoredAim> best;
        if (hitboxSet.count == 0 || evaluationsLeft <= 0)
            return best;

        auto&& weapon = localPawn.getActiveWeapon();
        const auto base = weapon.baseDamage();
        const auto rangeMod = weapon.rangeModifier();
        const auto armorRatio = weapon.armorRatio();
        const auto headshotMultiplier = weapon.headshotMultiplier();
        const bool haveWeapon = base.hasValue() && rangeMod.hasValue() && armorRatio.hasValue() && headshotMultiplier.hasValue();
        int armor = 0;
        bool hasHelmet = false;
        readTargetArmor(entity, armor, hasHelmet);
        const int minDamage = GET_CONFIG_VAR(aimbot_vars::MinDamage);

        for (int h = 0; h < hitboxSet.count && evaluationsLeft > 0; ++h) {
            const auto& entry = hitboxSet.entries[h];
            const int hitgroup = canonicalHitgroup(Hitboxes::hitgroupFromHitbox(entry.index));
            if (!hitgroupAllowed(hitgroup, flags))
                continue;
            if (entry.bone < 0 || entry.bone >= record.boneCount)
                continue;
            const auto& bone = record.bones[entry.bone];

            MultiPoint::Point points[MultiPoint::kMaxPoints];
            const int pointCount = MultiPoint::generate(entry, bone.position, bone.rotation,
                                                        static_cast<float>(GET_CONFIG_VAR(aimbot_vars::PointScale)), eye, 0.0f, false, points);

            for (int p = 0; p < pointCount && evaluationsLeft > 0; ++p) {
                --evaluationsLeft;
                const auto& point = points[p].position;

                
                const auto angles = shot_geometry::anglesTo(eye, point);
                const float dPitch = angles.pitch - pitch;
                const float dYaw = trig::normalizeDegrees(angles.yaw - yaw);
                if (dPitch * dPitch + dYaw * dYaw > maxFov * maxFov)
                    continue;

                float damage = kUnknownDamage;
                if (haveWeapon) {
                    const float dx = point.x - eye.x;
                    const float dy = point.y - eye.y;
                    const float dz = point.z - eye.z;
                    const float distance = trig::squareRoot(dx * dx + dy * dy + dz * dz);
                    damage = base.value() * fastmath::powf(rangeMod.value(), distance / 500.0f);
                    scaleDamage(damage, hitgroup, armor, hasHelmet, armorRatio.value(), headshotMultiplier.value());
                }
                if (minDamage > 0 && damage < static_cast<float>(minDamage))
                    continue;

                float score = damage;
                if (damage >= targetHealth)
                    score += kLethalBonus;
                score -= static_cast<float>(ageTicks) * kBacktrackTickPenalty;
                if (!best.hasValue() || score > best.value().score)
                    best = ScoredAim{typename Lagcomp<HookContext>::Result{point, hitgroup, record.simulationTime}, score};
            }
        }
        return best;
    }

    
    
    [[nodiscard]] Optional<typename Lagcomp<HookContext>::Result> bestRecordPoint(auto&& localPawn, const cs2::Vector& eye, float pitch, float yaw,
                                                                                  cs2::C_BaseEntity* entity,
                                                                                  const typename AimTarget<HookContext>::HitboxFlags& flags,
                                                                                  float maxFov, int maxTicks) const noexcept
    {
        auto&& node = hookContext.template make<BaseEntity>(entity).gameSceneNode();
        if (!node)
            return {};
        const auto hitboxSet = Hitboxes::query(node.raw());
        if (hitboxSet.count == 0)
            return {};

        auto&& lagcomp = hookContext.template make<Lagcomp>();
        const typename Lagcomp<HookContext>::Record* records[Lagcomp<HookContext>::kMaxRecords];
        const int recordCount = lagcomp.pickRecords(entity, maxTicks, records, Lagcomp<HookContext>::kMaxRecords);
        if (recordCount <= 0)
            return {};

        float targetHealth = 100.0f;
        auto&& targetPawn = hookContext.template make<BaseEntity>(entity).template as<PlayerPawn>();
        if (targetPawn)
            if (const auto hp = targetPawn.health(); hp.hasValue())
                targetHealth = static_cast<float>(hp.value());

        Optional<ScoredAim> best;
        int evaluationsLeft = kMaxScanPoints;
        for (int r = 0; r < recordCount && evaluationsLeft > 0; ++r) {
            const auto& record = *records[r];
            const int ageTicks = records[0]->tick - record.tick;
            auto scored = bestPointOnSkeleton(localPawn, eye, pitch, yaw, entity, flags, maxFov, hitboxSet, record, ageTicks, targetHealth, evaluationsLeft);
            if (scored.hasValue() && (!best.hasValue() || scored.value().score > best.value().score))
                best = scored;
        }
        if (!best.hasValue())
            return {};

        const auto& result = best.value().result;
        const auto angles = shot_geometry::anglesTo(eye, result.aimPoint);
        const typename AimTarget<HookContext>::Target winner{{angles.pitch, angles.yaw}, result.aimPoint, entity, result.hitgroup};
        if (!passesVisibility(localPawn, eye, winner))
            return {};
        return result;
    }

    
    
    
    
    
    [[nodiscard]] float estimatedDamage(auto&& localPawn, const cs2::Vector& eye, const typename AimTarget<HookContext>::Target& target) const noexcept
    {
        auto&& weapon = localPawn.getActiveWeapon();
        const auto base = weapon.baseDamage();
        const auto rangeMod = weapon.rangeModifier();
        const auto armorRatio = weapon.armorRatio();
        const auto headshotMultiplier = weapon.headshotMultiplier();
        if (!base.hasValue() || !rangeMod.hasValue() || !armorRatio.hasValue() || !headshotMultiplier.hasValue())
            return kUnknownDamage;

        const float dx = target.aimPoint.x - eye.x;
        const float dy = target.aimPoint.y - eye.y;
        const float dz = target.aimPoint.z - eye.z;
        const float distance = trig::squareRoot(dx * dx + dy * dy + dz * dz);

        float damage = base.value() * fastmath::powf(rangeMod.value(), distance / 500.0f);

        int armor = 0;
        bool hasHelmet = false;
        readTargetArmor(target.entity, armor, hasHelmet);

        scaleDamage(damage, target.hitgroup, armor, hasHelmet, armorRatio.value(), headshotMultiplier.value());
        return damage;
    }

    
    
    
    
    
    
    
    [[nodiscard]] bool passesVisibility(auto&& localPawn, const cs2::Vector& eye, const typename AimTarget<HookContext>::Target& target) const noexcept
    {
        const bool wallCheck = GET_CONFIG_VAR(aimbot_vars::WallCheck);
        const bool autowall = GET_CONFIG_VAR(aimbot_vars::Autowall);
        if (!wallCheck && !autowall)
            return true;

        const auto fwd = Tracing::traceLine(eye, target.aimPoint, localPawn.baseEntity());
        if (!fwd.valid)
            return false;
        if (fwd.reaches(target.entity))
            return true;
        if (!autowall || fwd.hitEntity != nullptr)
            return false;

        auto&& weapon = localPawn.getActiveWeapon();
        const auto base = weapon.baseDamage();
        const auto rangeMod = weapon.rangeModifier();
        const auto penPower = weapon.penetrationPower();
        if (!base.hasValue() || !rangeMod.hasValue() || !penPower.hasValue())
            return false;

        const float dx = target.aimPoint.x - eye.x;
        const float dy = target.aimPoint.y - eye.y;
        const float dz = target.aimPoint.z - eye.z;
        const float distance = trig::squareRoot(dx * dx + dy * dy + dz * dz);
        const float damageAtTarget = base.value() * fastmath::powf(rangeMod.value(), distance / 500.0f);

        const auto surviving = Autowall::penetratedDamage(eye, target.aimPoint, localPawn.baseEntity(),
                                                          target.entity, damageAtTarget, penPower.value());
        if (!surviving.hasValue())
            return false;

        int armor = 0;
        bool hasHelmet = false;
        readTargetArmor(target.entity, armor, hasHelmet);
        float survivingDamage = surviving.value();
        scaleDamage(survivingDamage, target.hitgroup, armor, hasHelmet,
                    weapon.armorRatio().valueOr(0.0f), weapon.headshotMultiplier().valueOr(1.0f));

        const int minDamage = GET_CONFIG_VAR(aimbot_vars::MinDamage);
        return floorNonNegative(survivingDamage) >= static_cast<float>(minDamage > 0 ? minDamage : 1);
    }


    static void scaleDamage(float& damage, int hitgroup, int armor, bool hasHelmet, float armorRatio, float headshotMultiplier) noexcept
    {
        switch (hitgroup) {
        case 1: damage *= headshotMultiplier; break; 
        case 3: damage *= 1.25f; break;              
        case 6: case 7: damage *= 0.75f; break;      
        default: break;                              
        }

        const bool isHead = hitgroup == 1;
        const bool isArmored = (hitgroup >= 1 && hitgroup <= 5) || hitgroup == 8;
        if (armor <= 0 || !isArmored || (isHead && !hasHelmet)) {
            damage = floorNonNegative(damage);
            return;
        }

        constexpr float armorBonus = 0.5f;
        const float armorRatioScaled = armorRatio * 0.5f;
        float damageToHealth = damage * armorRatioScaled;
        const float damageToArmor = (damage - damageToHealth) * armorBonus;
        if (damageToArmor > static_cast<float>(armor))
            damageToHealth = damage - (static_cast<float>(armor) / armorBonus);
        damage = floorNonNegative(damageToHealth);
    }

    
    
    [[nodiscard]] static float floorNonNegative(float value) noexcept
    {
        if (value < 0.0f)
            return 0.0f;
        return static_cast<float>(static_cast<int>(value));
    }

    
    
    
    void readTargetArmor(cs2::C_BaseEntity* entity, int& armorOut, bool& hasHelmetOut) const noexcept
    {
        if (!entity)
            return;
        auto&& schema = hookContext.schemaSystem();

        const auto armorOffset = schema.getFieldOffset("C_CSPlayerPawn", "m_ArmorValue");
        if (armorOffset.has_value() && *armorOffset > 0)
            std::memcpy(&armorOut, reinterpret_cast<const std::byte*>(entity) + *armorOffset, sizeof(armorOut));

        if (armorOut <= 0)
            return;

        const auto servicesOffset = schema.getFieldOffset("C_BasePlayerPawn", "m_pItemServices");
        if (!servicesOffset.has_value() || *servicesOffset <= 0)
            return;
        void* services{};
        std::memcpy(&services, reinterpret_cast<const std::byte*>(entity) + *servicesOffset, sizeof(services));
        if (!services)
            return;
        const auto helmetOffset = schema.getFieldOffset("CCSPlayer_ItemServices", "m_bHasHelmet");
        if (!helmetOffset.has_value() || *helmetOffset <= 0)
            return;
        std::memcpy(&hasHelmetOut, reinterpret_cast<const std::byte*>(services) + *helmetOffset, sizeof(hasHelmetOut));
    }

    
    
    
    
    [[nodiscard]] typename AimTarget<HookContext>::HitboxFlags hitboxFlags() const noexcept
    {
        if (GET_CONFIG_VAR(aimbot_vars::BodyAim))
            return {.head = false, .chest = true, .stomach = true, .arms = false, .legs = false};
        return {
            .head = GET_CONFIG_VAR(aimbot_vars::HitHead),
            .chest = GET_CONFIG_VAR(aimbot_vars::HitChest),
            .stomach = GET_CONFIG_VAR(aimbot_vars::HitStomach),
            .arms = GET_CONFIG_VAR(aimbot_vars::HitArms),
            .legs = GET_CONFIG_VAR(aimbot_vars::HitLegs),
        };
    }

    
    
    
    
    
    
    
    
    
    
    void autoStop(auto&& pawn, float viewYaw, const UserCmd& userCmd) const noexcept
    {
        const auto velocityOffset = hookContext.schemaSystem().getFieldOffset("C_BaseEntity", "m_vecVelocity");
        if (!velocityOffset.has_value() || *velocityOffset <= 0)
            return;

        const auto* const entityBytes = reinterpret_cast<const std::byte*>(static_cast<cs2::C_BaseEntity*>(pawn.baseEntity()));
        cs2::Vector velocity{};
        std::memcpy(&velocity, entityBytes + *velocityOffset, sizeof(velocity));

        
        const auto yawRad = viewYaw * trig::kDegreesToRadians;
        const auto sinYaw = trig::sine(yawRad);
        const auto cosYaw = trig::cosine(yawRad);

        const auto forwardSpeed = velocity.x * cosYaw + velocity.y * sinYaw;
        const auto sideSpeed = velocity.x * sinYaw - velocity.y * cosYaw;

        constexpr auto kInForward = std::uint64_t{1} << 3;
        constexpr auto kInBack = std::uint64_t{1} << 4;
        constexpr auto kInMoveLeft = std::uint64_t{1} << 9;
        constexpr auto kInMoveRight = std::uint64_t{1} << 10;

        std::uint64_t counterButtons = 0;
        if (forwardSpeed > kAutoStopSpeedThreshold)
            counterButtons |= kInBack;
        else if (forwardSpeed < -kAutoStopSpeedThreshold)
            counterButtons |= kInForward;
        if (sideSpeed > kAutoStopSpeedThreshold)
            counterButtons |= kInMoveLeft;
        else if (sideSpeed < -kAutoStopSpeedThreshold)
            counterButtons |= kInMoveRight;

        if (counterButtons)
            (void)userCmd.pressButtonsBothBanks(counterButtons); 
    }

    
    
    
    static constexpr int kHitchanceSamples = 256;
    static constexpr float kHitchanceRadius = 12.0f;

    
    
    
    static constexpr float kAutoStopSpeedThreshold = 60.0f;

    
    
    static constexpr float kUnknownDamage = 1000.0f;

    
    
    static constexpr int kMultipointSamples = 64;
    static constexpr float kMultipointHeadRadius = 3.5f;
    static constexpr float kMultipointBodyRadius = 10.0f;

    
    
    
    
    static constexpr float kLethalBonus = 25.0f;
    static constexpr float kBacktrackTickPenalty = 4.0f;
    static constexpr int kMaxScanPoints = 64;

    
    
    
    inline static bool forceShotThisTick{false};
    
    
    
    inline static bool shotStagedThisTick{false};
    
    inline static std::uint32_t lastTargetHandleValue{0};
    inline static float stagedAimPitch{0.0f};

    
    inline static const void* stickyMultipointEntity{nullptr};
    inline static int stickyMultipointHitgroup{-1};
    inline static int stickyMultipointSlot{0};
    inline static float stagedAimYaw{0.0f};
    inline static float stagedPunchPitch{0.0f};
    inline static float stagedPunchYaw{0.0f};
    inline static float stagedBacktrackSimTime{0.0f};
    inline static bool stagedSpreadCompensation{false};
    
    
    inline static bool waitArmed{false};
    inline static int waitArmedAtTick{-1};
    inline static int waitFireAtTick{-1};
    
    
    inline static cs2::Vector stagedEye{};
    inline static cs2::Vector stagedAimPoint{};
    
    
    
    using SolverAngles = typename SpreadSolver<HookContext>::Angles;
    inline static bool stagedCorrectionValid{false};
    inline static SolverAngles stagedCorrectionAngles{};

    
    
    static constexpr float kSeedFallbackRadius = 6.0f;

    HookContext& hookContext;
};
