#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include <CS2/Classes/CUserCmd.h>
#include <CS2/Classes/Entities/C_BaseEntity.h>
#include <CS2/Classes/Entities/C_CSPlayerPawn.h>
#include <CS2/Classes/Vector.h>
#include <Features/Combat/AimTarget.h>
#include <Features/Combat/AutoStop.h>
#include <Features/Combat/ShotWait.h>
#include <Features/Combat/AttackCommand.h>
#include <Features/Combat/Autowall/Autowall.h>
#include <Features/Combat/Autowall/DamageCache.h>
#include <Features/Combat/ShotGeometry.h>
#include <Features/Combat/SubtickShotWriter.h>
#include <Features/Game/FvaEmulator.h>
#include <GameClient/Hitboxes.h>
#include <GameClient/Lagcomp.h>
#include <GameClient/MultiPoint.h>
#include <Features/Combat/Aimbot/AimbotConfigVariables.h>
#include <Features/Combat/Aimbot/RageGroups.h>
#include <Features/Combat/Resolver/Resolver.h>
#include <GameClient/Entities/BaseEntity.h>
#include <GameClient/Entities/PlayerPawn.h>
#include <GameClient/ConVars/CvarSystem.h>
#include <GameClient/EntitySystem/EntitySystem.h>
#include <GameClient/SubtickMoves.h>
#include <GameClient/SpreadPrediction/SpreadSolver.h>
#include <GameClient/UserCmd.h>
#include <HookContext/HookContextMacros.h>
#include <Utils/FastMath.h>
#include <Utils/Optional.h>
#include <Utils/Trig.h>
#include <Utils/VerifyConsole.h>
#include <Utils/ColorUtils.h>
#include <GameClient/WorldToScreen/WorldToClipSpaceConverter.h>
#include <UI/ImGui/OverlayLayer.h>

template <typename HookContext>
class Aimbot {
public:
    explicit Aimbot(HookContext& hookContext) noexcept
        : hookContext{hookContext}
    {
    }

    void onCreateMove(cs2::CUserCmd* cmd) const noexcept
    {

        wallTraceBudget = {};
        damageCache.reset();
        healthCache.reset();
        if (multipointDebugActive && !GET_CONFIG_VAR(aimbot_vars::DebugMultipoints)) {
            overlay_layer::publishMultiPoints(nullptr, 0);
            multipointDebugActive = false;
        }
        forceShotThisTick = false;
        shotStagedThisTick = false;
        stagedCorrectionValid = false;
        spreadSamplesReady = false;
        spreadSamplesValid = false;

        if (!GET_CONFIG_VAR(aimbot_vars::Enabled)) {
            lastTargetHandleValue = 0;
            shotWait.reset();
            return;
        }

        const bool backtrackEnabled = GET_CONFIG_VAR(aimbot_vars::Backtrack);
        const bool extrapolateEnabled = GET_CONFIG_VAR(aimbot_vars::Extrapolate);

        if (GET_CONFIG_VAR(resolver_vars::Enabled)) {
            if (const auto tick = hookContext.localPlayerController().tickBase(); tick.hasValue())
                resolver::onTick(tick.value(), static_cast<int>(GET_CONFIG_VAR(resolver_vars::MissTicks)));
        }

        const UserCmd userCmd{cmd};

        auto&& localPawn = hookContext.activeLocalPlayerPawn();
        if (!localPawn || localPawn.isAlive() != true) {
            lastTargetHandleValue = 0;
            shotWait.reset();
            return;
        }

        // Per-weapon-group config from the active weapon (RageGroups.h). Non-gun
        // weapons (knife/grenade/C4) resolve invalid and the ragebot stands down.
        activeGroup = rage_groups::active(hookContext, localPawn.getActiveWeapon());
        if (!activeGroup.valid || !activeGroup.enabled) {
            lastTargetHandleValue = 0;
            shotWait.reset();
            return;
        }

        const bool forceShotEnabled = activeGroup.forceShot || activeGroup.forceShotAir;
        const bool userFiring = userCmd.isButtonDown(cs2::CCSGOInput::Buttons::kAttack);
        if (!userFiring && !forceShotEnabled) {
            lastTargetHandleValue = 0;
            shotWait.reset();
            return;
        }

        if (!userCmd) {
            lastTargetHandleValue = 0;
            shotWait.reset();
            return;
        }

        auto aimTarget = hookContext.template make<AimTarget>();
        const auto eye = aimTarget.eyePosition(localPawn);
        const auto currentPitch = userCmd.viewPitch();
        const auto currentYaw = userCmd.viewYaw();
        if (!eye.hasValue() || !currentPitch.hasValue() || !currentYaw.hasValue()
            || !hitbox_geometry::finite(eye.value()) || !std::isfinite(currentPitch.value()) || !std::isfinite(currentYaw.value())) {
            lastTargetHandleValue = 0;
            shotWait.reset();
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

        Optional<typename AimTarget<HookContext>::Target> refinedCandidate;
        const auto aim = aimTarget.acquire(eye.value(), currentPitch.value(), currentYaw.value(), maxFov,
            hitboxFlags(), [&](const auto& candidate) {
                if (passesVisibility(localPawn, eye.value(), candidate)) return true;
                if (!activeGroup.multipoint || !candidate.shape.valid) return false;
                const auto point = refineMultipoint(localPawn, eye.value(), candidate);
                if (!passesVisibility(localPawn, eye.value(), point)) return false;
                refinedCandidate = point;
                return true;
            },
            preferredTarget, static_cast<target_selection::Mode>(static_cast<std::uint8_t>(GET_CONFIG_VAR(aimbot_vars::TargetSelection))));
        if (!aim.hasValue()) {
            lastTargetHandleValue = 0;
            shotWait.reset();
            return;
        }
        lastTargetHandleValue = hookContext.template make<BaseEntity>(aim.value().entity).handle().value;

        auto chosen = aim.value();
        if (!chosen.shape.valid) chosen = withShape(chosen);
        if (refinedCandidate.hasValue() && refinedCandidate.value().entity == chosen.entity
            && refinedCandidate.value().hitgroup == chosen.hitgroup)
            chosen = refinedCandidate.value();
        else if (activeGroup.multipoint)
            chosen = refineMultipoint(localPawn, eye.value(), chosen);

        if (!passesVisibility(localPawn, eye.value(), chosen)) {
            shotWait.reset();
            return;
        }

        float backtrackSimTime = 0.0f;
        if (backtrackEnabled || extrapolateEnabled) {
            const auto flags = hitboxFlags();
            auto&& lagcomp = hookContext.template make<Lagcomp>();
            Optional<typename Lagcomp<HookContext>::Result> picked;
            bool historical = false;
            if (backtrackEnabled) {

                picked = bestRecordPoint(localPawn, eye.value(), currentPitch.value(), currentYaw.value(), chosen.entity, flags, maxFov, static_cast<int>(GET_CONFIG_VAR(aimbot_vars::BacktrackTicks)));
                historical = picked.hasValue();
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

            }
            if (picked.hasValue()) {
                const auto angles = shot_geometry::anglesTo(eye.value(), picked.value().aimPoint);
                chosen.angles = typename AimTarget<HookContext>::Angles{angles.pitch, angles.yaw};
                chosen.aimPoint = picked.value().aimPoint;
                chosen.hitgroup = picked.value().hitgroup;
                chosen.shape = picked.value().shape;

                const auto serverTick = hookContext.localPlayerController().tickBase();
                const int recordTick = static_cast<int>(picked.value().simulationTime / Lagcomp<HookContext>::kTickInterval);
                if (historical && (!serverTick.hasValue() || recordTick < serverTick.value()))
                    backtrackSimTime = picked.value().simulationTime;
            }
        }

        // Historical and extrapolated points need their own visibility gate.
        if (!passesVisibility(localPawn, eye.value(), chosen)) {
            shotWait.reset();
            return;
        }

        if (GET_CONFIG_VAR(aimbot_vars::AutoStop))
            autoStop(localPawn, currentYaw.value(), userCmd);

        forceShotThisTick = forceShotEnabled && shouldForceShoot(localPawn, eye.value(), chosen);
        if (!forceShotEnabled)
            shotWait.reset();
        if (!userFiring && !forceShotThisTick)
            return;

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

        if (GET_CONFIG_VAR(aimbot_vars::SpreadCompensation) && !stagedCorrectionValid)
            VerifyConsole::write(2.0f, "spread", "exact spread prediction unavailable; using uncompensated aim (gate will hold if enabled)");
        stagedAimPitch = chosen.angles.pitch;
        stagedAimYaw = chosen.angles.yaw;
        stagedPunchPitch = punchPitch;
        stagedPunchYaw = punchYaw;
        stagedBacktrackSimTime = backtrackSimTime;
        stagedSpreadCompensation = GET_CONFIG_VAR(aimbot_vars::SpreadCompensation);
        stagedEye = eye.value();
        stagedAimPoint = chosen.aimPoint;
        stagedShape = chosen.shape;
        shotStagedThisTick = true;
        if (GET_CONFIG_VAR(resolver_vars::Enabled)) {
            const auto tick = hookContext.localPlayerController().tickBase();
            resolver::onShotStaged(lastTargetHandleValue, tick.valueOr(-1));
        }
        stagedCommand = cmd;
        std::memcpy(&stagedSequence, reinterpret_cast<const std::byte*>(cmd) + cs2::CUserCmd::kCommandNumberOffset, sizeof(stagedSequence));
        lastTargetHandleValue = hookContext.template make<BaseEntity>(chosen.entity).handle().value;

        if (stagedCorrectionValid)
            fva::setTargetAngle(stagedCorrectionAngles.pitch, stagedCorrectionAngles.yaw);
        else
            fva::setTargetAngle(chosen.angles.pitch, chosen.angles.yaw);

    }

    void onWriteMoveCrc(cs2::CUserCmd* cmd) const noexcept
    {
        int sequence{};
        if (cmd) std::memcpy(&sequence, reinterpret_cast<const std::byte*>(cmd) + cs2::CUserCmd::kCommandNumberOffset, sizeof(sequence));
        if (!GET_CONFIG_VAR(aimbot_vars::Enabled) || cmd != stagedCommand || sequence != stagedSequence) {
            shotStagedThisTick = false;
            forceShotThisTick = false;
            return;
        }
        bool gateHeldFire = false;
        if (shotStagedThisTick) {
            shotStagedThisTick = false;
            auto&& localPawn = hookContext.activeLocalPlayerPawn();
            if (!localPawn || localPawn.isAlive() != true) { forceShotThisTick = false; return; }
            if (localPawn) {
                int redirectedEntry = -1;
                const bool backtrackShot = stagedBacktrackSimTime > 0.0f;
                const typename SpreadSolver<HookContext>::Angles* precomputed =
                    (!backtrackShot && stagedCorrectionValid) ? &stagedCorrectionAngles : nullptr;
                bool wrote = false;
                const bool lands = hookContext.template make<SubtickShotWriter>().run(cmd, localPawn, stagedAimPitch, stagedAimYaw,
                                                                                      stagedPunchPitch, stagedPunchYaw,
                                                                                      stagedBacktrackSimTime, stagedSpreadCompensation,
                                                                                      &redirectedEntry, precomputed, &wrote);
                if (!wrote) { forceShotThisTick = false; return; }

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

        if (!offset.hasValue())
            return false;
        const auto basis = shot_geometry::angleVectors(writtenPitch, writtenYaw);
        const cs2::Vector direction = shot_geometry::normalized(cs2::Vector{
            basis.forward.x + basis.left.x * offset.value().x + basis.up.x * offset.value().y,
            basis.forward.y + basis.left.y * offset.value().x + basis.up.y * offset.value().y,
            basis.forward.z + basis.left.z * offset.value().x + basis.up.z * offset.value().y,
        });
        return stagedShape.intersects(stagedEye, direction);
    }

    void onUnload() const noexcept
    {
        forceShotThisTick = false;
        shotStagedThisTick = false;
        lastTargetHandleValue = 0;
        shotWait.reset();
        stickyMultipointEntity = nullptr;
        stickyMultipointSlot = 0;
        activeGroup = {};
        multipointDebugActive = false;
    }

public:
    // Staged-shot query for the split-phase duck peek (its press/release decision
    // rides on whether this tick carries a shot).
    [[nodiscard]] bool shotStaged() const noexcept
    {
        return shotStagedThisTick;
    }

    // Duck peek (standing-inaccuracy exploit) armed: evaluate the cone the shot
    // actually sees - the duck is released for the fire tick (velocity's
    // get_standing_inaccuracy), not the live ducked cone.
    [[nodiscard]] bool duckPeekEval(auto&& localPawn) const noexcept
    {
        return GET_CONFIG_VAR(aimbot_vars::DuckPeek) && localPawn.isOnGround() == true;
    }

private:

    [[nodiscard]] bool shouldForceShoot(auto&& localPawn, const cs2::Vector& eye, const typename AimTarget<HookContext>::Target& target) const noexcept
    {
        const auto onGround = localPawn.isOnGround();
        const auto tick = hookContext.localPlayerController().tickBase();
        const auto ammo = localPawn.getActiveWeapon().clipAmmo();
        const bool enabled = onGround.hasValue() && (onGround.value()
            ? activeGroup.forceShot : activeGroup.forceShotAir);
        const int minDamage = activeGroup.minDamage;
        const bool eligible = enabled && ammo.hasValue() && ammo.value() > 0

            && (minDamage <= 0 || estimatedDamage(localPawn, eye, target) >= static_cast<float>(minDamage));
        const int chance = activeGroup.hitchance;
        const bool chancePassed = chance <= 0 || passesHitchance(localPawn, eye, target, chance);
        const bool maxAccuracy = localPawn.isAtMaxAccuracy() && chancePassed;
        const bool wait = GET_CONFIG_VAR(aimbot_vars::ForceShotWait);
        // Accuracy waiting is optional; zero hitchance means no probability gate.
        const bool accuracyReady = shotWait.ready(lastTargetHandleValue, tick.valueOr(-1), eligible, maxAccuracy,
                              chancePassed, wait,
                              static_cast<int>(GET_CONFIG_VAR(aimbot_vars::ForceShotWaitTicks)));
        return accuracyReady && localPawn.getActiveWeapon().isReadyToFire(tick.valueOr(-1)) == true;
    }

    [[nodiscard]] bool passesHitchance(auto&& localPawn, const cs2::Vector& eye, const typename AimTarget<HookContext>::Target& target, int thresholdPercent) const noexcept
    {
        const int fraction = hitchanceFraction(localPawn, eye, target, kHitchanceSamples);
        return fraction >= 0 && fraction >= thresholdPercent;
    }

    [[nodiscard]] int hitchanceFraction(auto&& localPawn, const cs2::Vector& eye, const typename AimTarget<HookContext>::Target& target, int samples) const noexcept
    {
        if (!target.shape.valid || samples <= 0 || !prepareSpreadSamples(localPawn))
            return -1;
        const auto basis = shot_geometry::angleVectors(target.angles.pitch, target.angles.yaw);
        int hits = 0;
        samples = std::min(samples, kHitchanceSamples);
        for (int sample = 0; sample < samples; ++sample) {
            const auto& spread = spreadSamples[sample];
            const cs2::Vector direction = shot_geometry::normalized(cs2::Vector{
                basis.forward.x + basis.left.x * spread.x + basis.up.x * spread.y,
                basis.forward.y + basis.left.y * spread.x + basis.up.y * spread.y,
                basis.forward.z + basis.left.z * spread.x + basis.up.z * spread.y,
            });
            if (target.shape.intersects(eye, direction))
                ++hits;
        }
        return hits * 100 / samples;
    }

    bool prepareSpreadSamples(auto&& localPawn) const noexcept
    {
        if (spreadSamplesReady) return spreadSamplesValid;
        spreadSamplesReady = true;
        auto solver = hookContext.template make<SpreadSolver>();
        const auto params = duckPeekEval(localPawn)
            ? solver.weaponParamsAtVelocity(localPawn.getActiveWeapon(), localPawn.baseEntity(), cs2::Vector{0.0f, 0.0f, 0.0f})
            : solver.weaponParams(localPawn.getActiveWeapon());
        if (!params.hasValue()) return false;
        for (int i = 0; i < kHitchanceSamples; ++i) {
            const auto offset = solver.estimatedSpreadOffset(static_cast<std::uint32_t>(i), params.value());
            if (!offset.hasValue()) return false;
            spreadSamples[i] = offset.value();
        }
        spreadSamplesValid = true;
        return true;
    }

    typename AimTarget<HookContext>::Target withShape(typename AimTarget<HookContext>::Target target) const noexcept
    {
        auto&& node = hookContext.template make<BaseEntity>(target.entity).gameSceneNode();
        if (!node) return target;
        const auto set = Hitboxes::query(node.raw());
        float nearest = 1.0e30f;
        for (int i = 0; i < set.count; ++i) {
            const auto& entry = set.entries[i];
            if (canonicalHitgroup(Hitboxes::hitgroupFromHitbox(entry.index)) != target.hitgroup) continue;
            const auto bone = node.boneTransform(entry.bone);
            if (!bone.hasValue()) continue;
            const auto shape = hitbox_geometry::from(entry, bone.value().position, bone.value().rotation, bone.value().scale);
            if (!shape.valid) continue;
            const auto delta = hitbox_geometry::subtract(shape.center(), target.aimPoint);
            const float distance = hitbox_geometry::dot(delta, delta);
            if (distance < nearest) { nearest = distance; target.shape = shape; }
        }
        return target;
    }

    static constexpr int kMultipointStickyBonus = 6;

    [[nodiscard]] typename AimTarget<HookContext>::Target refineMultipoint(auto&& localPawn, const cs2::Vector& eye, const typename AimTarget<HookContext>::Target& target) const noexcept
    {

        if (auto capsule = refineMultipointCapsule(localPawn, eye, target); capsule.hasValue())
            return capsule.value();

        return target;
    }

    // Debug multipoints (velocity debug_multipoints): publish the generated scan
    // cloud for the render overlay, colored per hitbox (velocity's map).
    void publishMultipointDebug(const MultiPoint::Point* points, int pointCount, int hitboxIndex) const noexcept
    {
        if (!GET_CONFIG_VAR(aimbot_vars::DebugMultipoints)) {
            if (multipointDebugActive) {
                overlay_layer::publishMultiPoints(nullptr, 0);
                multipointDebugActive = false;
            }
            return;
        }
        multipointDebugActive = true;
        auto&& converter = hookContext.template make<WorldToClipSpaceConverter>();
        overlay_layer::MultiPoint published[overlay_layer::kMaxMultiPoints];
        int count = 0;
        for (int i = 0; i < pointCount && count < overlay_layer::kMaxMultiPoints; ++i) {
            const auto& position = points[i].position;
            if (!hitbox_geometry::finite(position))
                continue;
            const auto clip = converter.toClipSpace(position);
            if (!(clip.w > 0.001f))
                continue;
            const auto inverseW = 1.0f / clip.w;
            published[count++] = overlay_layer::MultiPoint{
                (clip.x * inverseW + 1.0f) * 50.0f,
                (1.0f - clip.y * inverseW) * 50.0f,
                multipointDebugColor(hitboxIndex),
                points[i].isCenter,
            };
        }
        overlay_layer::publishMultiPoints(published, count);
    }

    [[nodiscard]] static std::uint32_t multipointDebugColor(int hitboxIndex) noexcept
    {
        switch (hitboxIndex) {
        case 0: return static_cast<std::uint32_t>(color::Rgba{255, 80, 80});
        case 2: case 3: return static_cast<std::uint32_t>(color::Rgba{220, 220, 60});
        case 4: case 5: case 6: return static_cast<std::uint32_t>(color::Rgba{255, 160, 60});
        case 7: case 8: case 9: case 10: case 11: case 12: return static_cast<std::uint32_t>(color::Rgba{80, 160, 255});
        case 13: case 14: case 15: case 16: case 17: case 18: return static_cast<std::uint32_t>(color::Rgba{180, 80, 255});
        default: return static_cast<std::uint32_t>(color::Rgba{200, 200, 200});
        }
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
            const auto shape = hitbox_geometry::from(entry, centre.value().position, centre.value().rotation, centre.value().scale);
            if (!shape.valid) continue;
            const auto p = shape.center();
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
                                                    static_cast<float>(activeGroup.pointScale), eye,
                                                    hookContext.localPlayerBulletInaccuracy().valueOr(0.0f),
                                                    activeGroup.dynamicPointscale, points, bone.value().scale);
        publishMultipointDebug(points, pointCount, aimed->index);
        if (pointCount <= 0)
            return {};

        const float bruteYaw = GET_CONFIG_VAR(resolver_vars::Enabled)
            ? resolver::bruteOffsetFor(hookContext.template make<BaseEntity>(target.entity).handle().value) : 0.0f;
        const cs2::Vector bruteCenter = target.aimPoint;

        const bool incumbent = stickyMultipointEntity == target.entity && stickyMultipointHitgroup == target.hitgroup;

        auto best = target;
        int bestSlot = 0;
        bool bestVisible = passesVisibility(localPawn, eye, target);
        int bestFraction = bestVisible ? hitchanceFraction(localPawn, eye, target, kMultipointSamples) : -1;
        if (bestFraction >= 0 && incumbent && stickyMultipointSlot == bestSlot)
            bestFraction += kMultipointStickyBonus;

        for (int i = 0; i < pointCount; ++i) {
            const auto point = rotateAround(points[i].position, bruteCenter, bruteYaw);
            const auto angles = shot_geometry::anglesTo(eye, point);
            const typename AimTarget<HookContext>::Target candidate{{angles.pitch, angles.yaw}, point, target.entity, target.hitgroup, target.shape};
            if (!passesVisibility(localPawn, eye, candidate))
                continue;
            int fraction = hitchanceFraction(localPawn, eye, candidate, kMultipointSamples);
            if (fraction >= 0 && incumbent && stickyMultipointSlot == i + 1)
                fraction += kMultipointStickyBonus;
            if (!bestVisible || fraction > bestFraction) {
                bestVisible = true;
                bestFraction = fraction;
                best = candidate;
                bestSlot = i + 1;
            }
        }

        stickyMultipointEntity = target.entity;
        stickyMultipointHitgroup = target.hitgroup;
        stickyMultipointSlot = bestSlot;
        return bestVisible ? Optional<typename AimTarget<HookContext>::Target>{best} : Optional<typename AimTarget<HookContext>::Target>{};
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

    // Resolver bruteforce rotation: yaw a victim's scan points around the
    // record origin by their current bruteforce state.
    [[nodiscard]] static cs2::Vector rotateAround(const cs2::Vector& point, const cs2::Vector& center, float yawDeg) noexcept
    {
        if (yawDeg == 0.0f)
            return point;
        const float radians = yawDeg * trig::kDegreesToRadians;
        const float c = trig::cosine(radians);
        const float s = trig::sine(radians);
        const float dx = point.x - center.x;
        const float dy = point.y - center.y;
        return cs2::Vector{center.x + dx * c - dy * s, center.y + dx * s + dy * c, point.z};
    }

    [[nodiscard]] Optional<ScoredAim> bestPointOnSkeleton(auto&& localPawn, const cs2::Vector& eye, float pitch, float yaw,
                                                          cs2::C_BaseEntity* entity,
                                                          const typename AimTarget<HookContext>::HitboxFlags& flags,
                                                          float maxFov, const Hitboxes::Set& hitboxSet,
                                                          const typename Lagcomp<HookContext>::Record& record,
                                                          int ageTicks, float targetHealth, int& evaluationsLeft, bool multipoint = true) const noexcept
    {
        Optional<ScoredAim> best;
        if (hitboxSet.count == 0 || evaluationsLeft <= 0)
            return best;

        const int minDamage = activeGroup.minDamage;
        const std::uint32_t victimHandleValue = hookContext.template make<BaseEntity>(entity).handle().value;

        for (int h = 0; h < hitboxSet.count && evaluationsLeft > 0; ++h) {
            const auto& entry = hitboxSet.entries[h];
            const int hitgroup = canonicalHitgroup(Hitboxes::hitgroupFromHitbox(entry.index));
            if (!hitgroupAllowed(hitgroup, flags))
                continue;
            if (entry.bone < 0 || entry.bone >= record.boneCount)
                continue;
            const auto& bone = record.bones[entry.bone];
            const auto shape = hitbox_geometry::from(entry, bone.position, bone.rotation, bone.scale);
            if (!shape.valid) continue;

            MultiPoint::Point points[MultiPoint::kMaxPoints];
            const int generatedCount = MultiPoint::generate(entry, bone.position, bone.rotation,
                                                        static_cast<float>(activeGroup.pointScale), eye, 0.0f, false, points, bone.scale);
            const int pointCount = multipoint && activeGroup.multipoint ? generatedCount : std::min(1, generatedCount);
            const float bruteYaw = GET_CONFIG_VAR(resolver_vars::Enabled)
                ? resolver::bruteOffsetFor(victimHandleValue) : 0.0f;
            const cs2::Vector bruteCenter = record.boneCount > 0 ? record.bones[0].position : cs2::Vector{};

            for (int p = 0; p < pointCount && evaluationsLeft > 0; ++p) {
                --evaluationsLeft;
                const auto point = rotateAround(points[p].position, bruteCenter, bruteYaw);

                const auto angles = shot_geometry::anglesTo(eye, point);
                const float dPitch = angles.pitch - pitch;
                const float dYaw = trig::normalizeDegrees(angles.yaw - yaw);
                if (dPitch * dPitch + dYaw * dYaw > maxFov * maxFov)
                    continue;

                const typename AimTarget<HookContext>::Target candidate{{angles.pitch, angles.yaw}, point, entity, hitgroup, shape};
                if (!passesVisibility(localPawn, eye, candidate))
                    continue;
                const float damage = estimatedDamage(localPawn, eye, candidate);
                // Lethal damage waives the group's min-damage gate (velocity).
                if (damage < 0.0f || (minDamage > 0 && damage < static_cast<float>(minDamage) && damage < targetHealth))
                    continue;

                float score = damage;
                if (damage >= targetHealth)
                    score += kLethalBonus;
                score -= static_cast<float>(ageTicks) * kBacktrackTickPenalty;
                if (!best.hasValue() || score > best.value().score)
                    best = ScoredAim{typename Lagcomp<HookContext>::Result{point, hitgroup, record.simulationTime, shape}, score};
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
        const int passes = activeGroup.multipoint ? 2 : 1;
        for (int pass = 0; pass < passes && evaluationsLeft > 0; ++pass) {
            for (int r = 0; r < recordCount && evaluationsLeft > 0; ++r) {
                const auto& record = *records[r];
                const int ageTicks = records[0]->tick - record.tick;
                // A per-record allowance preserves history coverage under load.
                int budget = std::min(evaluationsLeft, pass == 0 ? 5 : 12);
                const int initialBudget = budget;
                auto scored = bestPointOnSkeleton(localPawn, eye, pitch, yaw, entity, flags, maxFov, hitboxSet,
                                                  record, ageTicks, targetHealth, budget, pass != 0);
                evaluationsLeft -= initialBudget - budget;
                if (scored.hasValue() && (!best.hasValue() || scored.value().score > best.value().score))
                    best = scored;
            }
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

    [[nodiscard]] float estimatedDamage(auto&& localPawn, const cs2::Vector& eye,
                                       const typename AimTarget<HookContext>::Target& target) const noexcept
    {
        if (const auto cached = damageCache.find(eye, target.aimPoint, target.entity, target.hitgroup); cached.hasValue())
            return cached.value();
        const float damage = computeDamage(localPawn, eye, target);
        damageCache.store(eye, target.aimPoint, target.entity, target.hitgroup, damage);
        return damage;
    }

    [[nodiscard]] float computeDamage(auto&& localPawn, const cs2::Vector& eye,
        const typename AimTarget<HookContext>::Target& target) const noexcept
    {
        auto&& weapon = localPawn.getActiveWeapon();
        const auto base = weapon.baseDamage();
        const auto range = weapon.rangeModifier();
        const auto maxRange = weapon.maxRange();
        const auto armorRatio = weapon.armorRatio();
        const auto headMultiplier = weapon.headshotMultiplier();
        if (!base.hasValue() || !range.hasValue() || !maxRange.hasValue() || !armorRatio.hasValue() || !headMultiplier.hasValue()
            || !std::isfinite(base.value()) || !std::isfinite(range.value()) || base.value()<=0
            || range.value()<=0 || range.value()>1 || !std::isfinite(armorRatio.value())
            || armorRatio.value()<0 || !std::isfinite(headMultiplier.value()) || headMultiplier.value()<=0)
            return kUnknownDamage;
        const auto delta = hitbox_geometry::subtract(target.aimPoint,eye);
        const float distance = std::hypot(delta.x, delta.y, delta.z);
        if (!std::isfinite(distance) || !std::isfinite(maxRange.value()) || maxRange.value() <= 0.0f
            || distance > maxRange.value()) return kUnknownDamage;
        float damage = penetration::decay(base.value(), distance, range.value());
        int actualHitgroup = target.hitgroup;
        const bool wallCheck = GET_CONFIG_VAR(aimbot_vars::WallCheck);
        const bool autowall = GET_CONFIG_VAR(aimbot_vars::Autowall);
        // Native bullet simulation supplies the actual struck hitgroup even
        // for a clear shot: a ray to the head can strike an arm first.
        if (autowall) {
            const auto impact = Autowall::evaluate(eye, target.aimPoint, localPawn.baseEntity(), target.entity,
                {base.value(), weapon.penetrationPower().valueOr(0.0f), range.value(), maxRange.value()}, {},
                wallTraceBudget, hookContext);
            if (!impact.hasValue()) return kUnknownDamage;
            damage = impact.value().damage;
            actualHitgroup = impact.value().hitgroup;
        } else if (wallCheck) {
            if (!wallTraceBudget.take()) return kUnknownDamage;
            const auto trace = Tracing::traceLine(eye, target.aimPoint, localPawn.baseEntity(), Autowall::kBulletMask);
            if (!trace.reaches(target.entity)) return kUnknownDamage;
        }
        return Autowall::healthDamage(hookContext, target.entity, damage, actualHitgroup,
            armorRatio.value(), headMultiplier.value()).valueOr(kUnknownDamage);
    }

    [[nodiscard]] bool passesVisibility(auto&& localPawn, const cs2::Vector& eye,
                                       const typename AimTarget<HookContext>::Target& target) const noexcept
    {
        const float damage = estimatedDamage(localPawn,eye,target);
        const int minimum = activeGroup.minDamage;
        const float needed = static_cast<float>(minimum>0 ? minimum : 1);
        if (damage >= needed)
            return true;
        // Lethal damage waives the gate (velocity: damage >= target health).
        const auto health = targetHealth(target.entity);
        return health.hasValue() && damage >= health.value();
    }

    // Per-tick entity->health memo (lethal waiver reads it per visibility probe).
    inline static struct HealthCache {
        static constexpr std::size_t kCapacity = 32;
        struct Entry { const void* entity; float health; };
        std::array<Entry, kCapacity> entries{};
        std::size_t size = 0;
        void reset() noexcept { size = 0; }
        [[nodiscard]] bool find(const void* entity, float& out) const noexcept
        {
            for (std::size_t i = 0; i < size; ++i)
                if (entries[i].entity == entity) { out = entries[i].health; return true; }
            return false;
        }
        void store(const void* entity, float health) noexcept
        {
            if (size < kCapacity)
                entries[size++] = {entity, health};
        }
    } healthCache{};

    [[nodiscard]] Optional<float> targetHealth(cs2::C_BaseEntity* entity) const noexcept
    {
        if (!entity)
            return {};
        float cached;
        if (healthCache.find(entity, cached))
            return cached;
        float health = 100.0f;
        auto&& targetPawn = hookContext.template make<BaseEntity>(entity).template as<PlayerPawn>();
        if (targetPawn)
            if (const auto hp = targetPawn.health(); hp.hasValue())
                health = static_cast<float>(hp.value());
        healthCache.store(entity, health);
        return health;
    }

    [[nodiscard]] typename AimTarget<HookContext>::HitboxFlags hitboxFlags() const noexcept
    {
        if (activeGroup.bodyAim)
            return {.head = false, .chest = true, .stomach = true, .arms = false, .legs = false};
        return {
            .head = activeGroup.hitHead,
            .chest = activeGroup.hitChest,
            .stomach = activeGroup.hitStomach,
            .arms = activeGroup.hitArms,
            .legs = activeGroup.hitLegs,
        };
    }

    void autoStop(auto&& pawn, float viewYaw, const UserCmd& userCmd) const noexcept
    {
        if (pawn.isOnGround() != true)
            return;
        auto&& schema = hookContext.schemaSystem();
        const auto velocityOffset = schema.getFieldOffset("C_BaseEntity", "m_vecVelocity");
        if (!velocityOffset.has_value() || *velocityOffset <= 0)
            return;
        const auto* const bytes = reinterpret_cast<const std::byte*>(static_cast<cs2::C_BaseEntity*>(pawn.baseEntity()));
        cs2::Vector velocity{};
        std::memcpy(&velocity, bytes + *velocityOffset, sizeof(velocity));

        auto_stop::StopInputs inputs;
        auto&& cvars = hookContext.template make<CvarSystem>();
        inputs.svFriction = cvars.readFloatConVar("sv_friction");
        inputs.svStopspeed = cvars.readFloatConVar("sv_stopspeed");
        const auto surfaceOffset = schema.getFieldOffset("C_BaseEntity", "m_flSurfaceFriction");
        if (surfaceOffset.has_value() && *surfaceOffset > 0)
            std::memcpy(&inputs.surfaceFriction.emplace(), bytes + *surfaceOffset, sizeof(float));
        if (const auto weaponMax = pawn.getActiveWeapon().maxSpeed(); weaponMax.hasValue())
            inputs.weaponMaxSpeed = weaponMax.value();
        auto_stop::apply(userCmd, velocity, viewYaw, inputs);
        SubtickMoves<HookContext>::stripAnalog(userCmd.baseMessage());
        SubtickMoves<HookContext>::releaseButton(userCmd.baseMessage(), auto_stop::forward);
        SubtickMoves<HookContext>::releaseButton(userCmd.baseMessage(), auto_stop::back);
        SubtickMoves<HookContext>::releaseButton(userCmd.baseMessage(), auto_stop::left);
        SubtickMoves<HookContext>::releaseButton(userCmd.baseMessage(), auto_stop::right);
    }

    static constexpr int kHitchanceSamples = 256;

    static constexpr float kUnknownDamage = -1.0f;

    static constexpr int kMultipointSamples = 64;

    static constexpr float kLethalBonus = 25.0f;
    static constexpr float kBacktrackTickPenalty = 4.0f;
    static constexpr int kMaxScanPoints = 64;

    inline static cs2::CUserCmd* stagedCommand{};
    inline static int stagedSequence{};
    inline static rage_groups::Active activeGroup{};
    inline static hitbox_geometry::Shape stagedShape{};
    inline static bool spreadSamplesReady{false}, spreadSamplesValid{false};
    inline static cs2::Vector spreadSamples[kHitchanceSamples]{};
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

    inline static shot_wait::State shotWait;
    inline static penetration::TraceBudget wallTraceBudget;
    inline static penetration::DamageCache damageCache;
    inline static bool multipointDebugActive{false};

    inline static cs2::Vector stagedEye{};
    inline static cs2::Vector stagedAimPoint{};

    using SolverAngles = typename SpreadSolver<HookContext>::Angles;
    inline static bool stagedCorrectionValid{false};
    inline static SolverAngles stagedCorrectionAngles{};

    HookContext& hookContext;
};
