#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

#include <CS2/Classes/CUserCmd.h>
#include <CS2/Classes/Entities/C_BaseEntity.h>
#include <CS2/Classes/Entities/C_CSPlayerPawn.h>
#include <CS2/Classes/EntitySystem/CEntityIndex.h>
#include <CS2/Classes/Vector.h>
#include <CS2/Constants/DllNames.h>
#include <Features/Combat/AttackCommand.h>
#include <Features/Combat/Autowall/Autowall.h>
#include <Features/Combat/HitboxGeometry.h>
#include <Features/Combat/ShotGeometry.h>
#include <Features/Combat/Rcs/RcsConfigVariables.h>
#include <Features/Combat/SubtickShotWriter.h>
#include <Features/Combat/Triggerbot/TriggerbotConfigVariables.h>
#include <Features/Combat/Triggerbot/TriggerbotState.h>
#include <GameClient/Hitboxes.h>
#include <GameClient/Bind.h>
#include <GameClient/Entities/BaseEntity.h>
#include <GameClient/Entities/PlayerPawn.h>
#include <GameClient/EntitySystem/EntitySystem.h>
#include <GameClient/Lagcomp.h>
#include <GameClient/MouseState.h>
#include <GameClient/SpreadPrediction/SpreadSolver.h>
#include <GameClient/Tracing/Tracing.h>
#include <GameClient/UserCmd.h>
#include <HookContext/HookContextMacros.h>
#include <SDL/SdlFunctions.h>
#include <Utils/Optional.h>
#include <Utils/Trig.h>
#include <Utils/VerifyConsole.h>


























template <typename HookContext>
class Triggerbot {
public:
    explicit Triggerbot(HookContext& hookContext) noexcept
        : hookContext{hookContext}
    {
    }

    
    
    static void disarmStatics() noexcept
    {
        armed = false;
        armedTarget = nullptr;
        fireAtTime = 0.0f;
        triggerbot_state::latched = false;
    }

    
    
    
    
    void onWriteMoveCrc(cs2::CUserCmd* cmd) const noexcept
    {
        // hold = active while key down; toggle = rising-edge latch. latch
        // update runs before any early-out so a release is never missed.
        const bool enabled = GET_CONFIG_VAR(triggerbot_vars::Enabled);
        const bool toggleMode = GET_CONFIG_VAR(triggerbot_vars::KeyMode) != 0;
        const bool keyDown = Bind::isDown(GET_CONFIG_VAR(triggerbot_vars::HoldKey));
        if (toggleMode) {
            if (GET_CONFIG_VAR(triggerbot_vars::HoldKey) == Bind::kOff || !enabled)
                triggerbot_state::latched = false;
            else if (keyDown && !triggerbot_state::lastKeyWasDown)
                triggerbot_state::latched = !triggerbot_state::latched;
        } else {
            triggerbot_state::latched = false;
        }
        triggerbot_state::lastKeyWasDown = keyDown;

        if (!enabled || !(toggleMode ? triggerbot_state::latched : keyDown)) {
            if (enabled)
            disarm();
            return;
        }

        const auto now = hookContext.globalVars().curtime();
        if (!now.hasValue()) {
            disarm();
            return;
        }

        
        
        
        static float lastSeenCurtime = -1.0f;
        if (lastSeenCurtime > 0.0f && now.value() < lastSeenCurtime) {
            armed = false;
            fireAtTime = 0.0f;
        }
        lastSeenCurtime = now.value();

        // target routes in order: direct crosshair pawn, then (only if the
        // crosshair found nothing) the through-wall scan, then the backtrack
        // ghost. scan targets carry their own resolved point + hitgroup.
        ScanTarget selection{};
        if (const auto direct = crosshairTarget()) {
            selection.pawn = direct.rawPawn();
        } else {
            const auto scanned = scanTarget(cmd);
            if (!scanned.hasValue()) {
                disarm();
                return;
            }
            selection = scanned.value();
        }

        auto&& target = hookContext.template make<PlayerPawn>(selection.pawn);
        auto* const rawTarget = static_cast<cs2::C_BaseEntity*>(target.baseEntity());
        if (!armed || armedTarget != rawTarget) {
            armed = true;
            armedTarget = rawTarget;
            firedAtTarget = false;
            // crosshair must hold on this target for AimTimeMs before the
            // reaction delay starts.
            const float aimTime = static_cast<float>(GET_CONFIG_VAR(triggerbot_vars::AimTimeMs)) / 1000.0f;
            const float delay = delaySeconds();
            fireAtTime = now.value() + humanizeDelay(aimTime + delay);
            return;
        }

        if (now.value() < fireAtTime)
            return;

        // first shot at a target runs the full gates; refires skip them so
        // semi-autos keep firing at weapon speed while held on target.
        if (!GET_CONFIG_VAR(triggerbot_vars::AutoRefire) || !firedAtTarget) {
            if (!wouldShotLand(target)) {
                return;
            }
            if (!passesMaxAccuracyGate()) {
                return;
            }
        }
        if (selection.fromBacktrack) {
            // backtrack ghost: the direct trace already failed; the live-pose
            // gates are the wrong question, the record's own hitgroup decides.
            if (!hitgroupWanted(selection.hitgroup, wantedHitboxes())) {
                return;
            }
        } else if (selection.throughWall) {
            // through-wall: the scan already damage-checked the bone point;
            // re-check the crosshair direction penetrates for enough damage.
            if (!passesVisibility(target, cmd)) {
                return;
            }
            if (!hitgroupWanted(selection.hitgroup, wantedHitboxes())) {
                return;
            }
        } else {
            if (!passesVisibility(target, cmd)) {
                return;
            }
            if (!passesAimGates(target, cmd)) {
                return;
            }
            if (!passesSeededFire(target, cmd)) {
                return;
            }
        }

        if (!hookContext.template make<AttackCommand>().press(cmd))
            return;
        firedAtTarget = true;
        // refire delay paces follow-up shots at the same target.
        const float refireDelay = static_cast<float>(GET_CONFIG_VAR(triggerbot_vars::RefireDelayMs)) / 1000.0f;
        if (refireDelay > 0.0f)
            fireAtTime = now.value() + humanizeDelay(refireDelay);

        
        
        
        
        
        
        
        
        
        
        
        // A backtrack ghost needs its record's sim time stamped into the shot -
        // without it the server resolves on the LIVE tick against the moved pose
        // and the shot sails past (the ghost's crosshair ray missed by
        // construction). Live shots keep the old writer behavior: touched only
        // under spread compensation.
        const float backtrackSimTime = selection.fromBacktrack ? selection.simulationTime : 0.0f;
        const bool compensateSpread = GET_CONFIG_VAR(triggerbot_vars::SpreadCompensation);
        if (backtrackSimTime > 0.0f || compensateSpread) {
            auto&& localPawn = hookContext.activeLocalPlayerPawn();
            const UserCmd firedCmd{cmd};
            const auto aimPitch = firedCmd.viewPitch();
            const auto aimYaw = firedCmd.viewYaw();
            if (localPawn && aimPitch.hasValue() && aimYaw.hasValue()) {
                float punchPitch = 0.0f, punchYaw = 0.0f;
                const auto shots = localPawn.shotsFired();
                const bool rcsApplied = GET_CONFIG_VAR(rcs_vars::Enabled) && shots.hasValue() && shots.value() >= 1;
                if (const auto punch = localPawn.aimPunchAngle(); !rcsApplied && punch.hasValue()) {
                    punchPitch = punch.value().x;
                    punchYaw = punch.value().y;
                }
                
                
                
                static_cast<void>(hookContext.template make<SubtickShotWriter>().run(cmd, localPawn,
                                                                                   aimPitch.value(), aimYaw.value(),
                                                                                   punchPitch, punchYaw,
                                                                                   backtrackSimTime,
                                                                                   compensateSpread));
            }
        }
    }

    void onUnload() const noexcept
    {
        disarm();
    }

private:
    // resolved by a fallback scan (through-wall or backtrack), not the direct
    // crosshair trace. point/hitgroup are the scan's own resolution.
    struct ScanTarget {
        cs2::C_CSPlayerPawn* pawn{};
        cs2::Vector point{};
        int hitgroup{0};
        bool throughWall{false};
        bool fromBacktrack{false};
        // A backtrack ghost's record sim time - the shot must be stamped with it
        // or the server resolves on the LIVE tick against the moved pose.
        float simulationTime{};
    };

    // The five triggerbot hitbox toggles (the ragebot's set).
    struct HitboxWants {
        bool head, chest, stomach, arms, legs;
    };
    [[nodiscard]] HitboxWants wantedHitboxes() const noexcept
    {
        return {GET_CONFIG_VAR(triggerbot_vars::HitHead) != false,
            GET_CONFIG_VAR(triggerbot_vars::HitChest) != false,
            GET_CONFIG_VAR(triggerbot_vars::HitStomach) != false,
            GET_CONFIG_VAR(triggerbot_vars::HitArms) != false,
            GET_CONFIG_VAR(triggerbot_vars::HitLegs) != false};
    }

    // the hitbox the crosshair ray actually lands on, so group selection and
    // the hitchance sampler run against what we'd hit.
    struct MatchedHitbox {
        hitbox_geometry::Shape shape;
        int hitgroup;
        bool fromFallback;
        cs2::Vector fallbackAim{};
        float fallbackRadius{0.0f};
    };

    // hitgroup map: 1 head, 2 chest, 3 stomach, 4|5 arms, 6|7 legs, 8 gear.
    [[nodiscard]] static bool hitgroupWanted(int hitgroup, const HitboxWants& wants) noexcept
    {
        return Hitboxes::hitgroupWantedBySelection(hitgroup, wants.head, wants.chest, wants.stomach, wants.arms, wants.legs);
    }


    // fallback order: through-wall scan (closest-to-crosshair enemy hitbox
    // that still takes >= MinDamage through the wall), then the backtrack
    // ghost (crosshair on an enemy's past position; server lag comp lands it).
    [[nodiscard]] Optional<ScanTarget> scanTarget(cs2::CUserCmd* cmd) const noexcept
    {
        if (GET_CONFIG_VAR(triggerbot_vars::ThroughWalls))
            if (const auto throughWall = throughWallTarget(cmd); throughWall.hasValue())
                return throughWall;
        if (GET_CONFIG_VAR(triggerbot_vars::Backtrack))
            if (const auto backtrack = backtrackTarget(cmd); backtrack.hasValue())
                return backtrack;
        return {};
    }

    [[nodiscard]] Optional<ScanTarget> throughWallTarget(cs2::CUserCmd* cmd) const noexcept
    {
        auto&& localPawn = hookContext.activeLocalPlayerPawn();
        if (!localPawn || localPawn.isAlive() != true)
            return {};

        const auto bullets = localPawn.getActiveWeapon().numBullets();
        if (bullets.hasValue() && bullets.value() <= 0)
            return {};

        const auto eye = localPawn.eyePosition();
        const UserCmd command{cmd};
        const auto pitch = command.viewPitch();
        const auto yaw = command.viewYaw();
        if (!eye.hasValue() || !pitch.hasValue() || !yaw.hasValue())
            return {};

        auto&& weapon = localPawn.getActiveWeapon();
        const auto damage = weapon.baseDamage();
        const auto rangeModifier = weapon.rangeModifier();
        const auto maxRange = weapon.maxRange();
        if (!damage.hasValue() || !rangeModifier.hasValue() || !maxRange.hasValue() || !std::isfinite(maxRange.value()) || maxRange.value() <= 0)
            return {};
        const auto armorRatio = weapon.armorRatio();
        const auto headMultiplier = weapon.headshotMultiplier();
        if (!armorRatio.hasValue() || !headMultiplier.hasValue())
            return {};

        const int thickness = GET_CONFIG_VAR(triggerbot_vars::Autowall) ? static_cast<int>(GET_CONFIG_VAR(triggerbot_vars::AutowallMaxThickness)) : 8;
        const float minDamage = static_cast<float>(GET_CONFIG_VAR(triggerbot_vars::MinDamage));

        void* const skip = static_cast<cs2::C_BaseEntity*>(localPawn.baseEntity());
        Optional<ScanTarget> best;
        float bestFov = kScanFov;

        hookContext.template make<EntitySystem>().forEachNetworkableEntityIdentity([&](const auto& identity) {
            auto&& baseEntity = hookContext.template make<BaseEntity>(static_cast<cs2::C_BaseEntity*>(identity.entity));
            if (!baseEntity.classify().template is<cs2::C_CSPlayerPawn>())
                return;
            auto&& enemy = baseEntity.template as<PlayerPawn>();
            if (!enemy || enemy.isControlledByLocalPlayer() || enemy.isEnemy() != true || enemy.isAlive() != true)
                return;
            const auto health = enemy.health();
            if (!health.hasValue() || health.value() <= 0)
                return;
            if (const auto immune = enemy.hasImmunity(); immune.hasValue() && immune.value())
                return;

            static constexpr int kBones[]{kHeadBone, kChestBone, kStomachBone};
            static constexpr int kHitgroups[]{1, 2, 3};
            auto&& node = enemy.baseEntity().gameSceneNode();

            // scan the game's own hitbox set (true centers, true hitgroups),
            // same data the aimbot uses. falls back to the old bone loop when
            // the chain is stale.
            const auto hitboxSet = Hitboxes::query(node.raw());
            const int scanCount = hitboxSet.count > 0 ? hitboxSet.count : 3;
            for (int b = 0; b < scanCount; ++b) {
                cs2::Vector point{};
                int hitgroup = 0;
                if (hitboxSet.count > 0) {
                    const auto& entry = hitboxSet.entries[b];
                    hitgroup = Hitboxes::hitgroupFromHitbox(entry.index);
                    const auto shape = worldHitboxShape(node, entry);
                    if (!shape.hasValue())
                        continue;
                    point = shape.value().center();
                } else {
                    point = node.bonePosition(kBones[b]).valueOr(cs2::Vector{});
                    hitgroup = kHitgroups[b];
                    if (point.x == 0.0f && point.y == 0.0f && point.z == 0.0f)
                        continue;
                }
                if (!hitgroupWanted(hitgroup, wantedHitboxes()))
                    continue;
                const auto angles = shot_geometry::anglesTo(eye.value(), point);
                const float dPitch = angles.pitch - pitch.value();
                const float dYaw = trig::normalizeDegrees(angles.yaw - yaw.value());
                const float fov = trig::squareRoot(dPitch * dPitch + dYaw * dYaw);
                if (fov > bestFov)
                    continue;

                penetration::TraceBudget budget;
                const penetration::Limits limits{90.0f, float(thickness), 4};
                void* const targetEntity = static_cast<cs2::C_BaseEntity*>(enemy.baseEntity());
                const auto impact = Autowall::evaluate(eye.value(), point, skip, targetEntity,
                    {damage.value(), weapon.penetrationPower().valueOr(0.0f), rangeModifier.value(), maxRange.value()},
                    limits, budget, hookContext);
                if (!impact.hasValue())
                    continue;
                const auto healthDamage = Autowall::healthDamage(hookContext, targetEntity, impact.value().damage, impact.value().hitgroup,
                    armorRatio.value(), headMultiplier.value());
                if (!healthDamage.hasValue() || healthDamage.value() < (minDamage > 1.0f ? minDamage : 1.0f))
                    continue;

                bestFov = fov;
                best = ScanTarget{enemy.rawPawn(), point, hitgroup, true, false};
            }
        });
        return best;
    }

    [[nodiscard]] Optional<ScanTarget> backtrackTarget(cs2::CUserCmd* cmd) const noexcept
    {
        auto&& localPawn = hookContext.activeLocalPlayerPawn();
        if (!localPawn || localPawn.isAlive() != true)
            return {};

        const auto eye = localPawn.eyePosition();
        const UserCmd command{cmd};
        const auto pitch = command.viewPitch();
        const auto yaw = command.viewYaw();
        if (!eye.hasValue() || !pitch.hasValue() || !yaw.hasValue())
            return {};

        const int maxTicks = static_cast<int>(GET_CONFIG_VAR(triggerbot_vars::BacktrackTicks));
        const cs2::Vector direction = shot_geometry::angleVectors(pitch.value(), yaw.value()).forward;

        // Per-bone selection forwarded to the record scan (head/chest/stomach).
        const HitboxWants wants = wantedHitboxes();

        auto&& lagcomp = hookContext.template make<Lagcomp>();
        Optional<ScanTarget> best;

        hookContext.template make<EntitySystem>().forEachNetworkableEntityIdentity([&](const auto& identity) {
            if (best.hasValue())
                return;
            auto&& baseEntity = hookContext.template make<BaseEntity>(static_cast<cs2::C_BaseEntity*>(identity.entity));
            if (!baseEntity.classify().template is<cs2::C_CSPlayerPawn>())
                return;
            auto&& enemy = baseEntity.template as<PlayerPawn>();
            if (!enemy || enemy.isControlledByLocalPlayer() || enemy.isEnemy() != true || enemy.isAlive() != true)
                return;

            // the crosshair ray must cross the true capsule (model extents +
            // the record's bone snapshot), not an invented radius sphere.
            const auto record = lagcomp.bestRecord(static_cast<cs2::C_BaseEntity*>(enemy.baseEntity()), eye.value(), pitch.value(), yaw.value(),
                                                   wants.head, wants.chest, wants.stomach, wants.arms, wants.legs, maxTicks);
            if (!record.hasValue())
                return;
            if (!hitgroupWanted(record.value().hitgroup, wants))
                return;
            if (!record.value().shape.valid || !record.value().shape.intersects(eye.value(), direction))
                return;
            best = ScanTarget{enemy.rawPawn(), record.value().aimPoint, record.value().hitgroup, false, true, record.value().simulationTime};
        });
        return best;
    }

    [[nodiscard]] PlayerPawn<HookContext> crosshairTarget() const noexcept
    {
        const auto none = hookContext.template make<PlayerPawn>(static_cast<cs2::C_CSPlayerPawn*>(nullptr));

        auto&& localPawn = hookContext.activeLocalPlayerPawn();
        if (!localPawn)
            return none;

        if (localPawn.isAlive() != true)
            return none;

        
        
        
        const auto bullets = localPawn.getActiveWeapon().numBullets();
        if (bullets.hasValue() && bullets.value() <= 0)
            return none;

        const auto targetIndex = crosshairEntityIndex(localPawn);
        if (!targetIndex.hasValue())
            return none;

        auto* const entity = hookContext.template make<EntitySystem>().getEntityFromIndex(cs2::CEntityIndex{targetIndex.value()});
        if (!entity)
            return none;

        
        
        auto&& baseEntity = hookContext.template make<BaseEntity>(static_cast<cs2::C_BaseEntity*>(entity));
        if (!baseEntity.classify().template is<cs2::C_CSPlayerPawn>())
            return none;

        auto&& target = baseEntity.template as<PlayerPawn>();
        if (!target || target.isControlledByLocalPlayer())
            return none;

        
        
        if (target.isEnemy() != true)
            return none;

        const auto health = target.health();
        if (!health.hasValue() || health.value() <= 0)
            return none;

        
        if (const auto immune = target.hasImmunity(); immune.hasValue() && immune.value())
            return none;

        
        
        
        
        
        
        
        return target;
    }

    
    
    
    
    
    
    [[nodiscard]] bool wouldShotLand(auto&& target) const noexcept
    {
        if (!GET_CONFIG_VAR(triggerbot_vars::AccuracyCheck))
            return true;

        const auto cone = hookContext.localPlayerBulletInaccuracy();
        if (!cone.hasValue())
            return true;

        const auto localOrigin = hookContext.activeLocalPlayerPawn().absOrigin();
        const auto targetOrigin = target.absOrigin();
        if (!localOrigin.hasValue() || !targetOrigin.hasValue())
            return true;

        const auto dx = targetOrigin.value().x - localOrigin.value().x;
        const auto dy = targetOrigin.value().y - localOrigin.value().y;
        const auto dz = targetOrigin.value().z - localOrigin.value().z;
        const auto distanceSquared = dx * dx + dy * dy + dz * dz;

        
        
        const auto radius = static_cast<float>(GET_CONFIG_VAR(triggerbot_vars::AccuracyRadius));
        const auto maxMissSquared = cone.value() * cone.value() * distanceSquared;
        return maxMissSquared <= radius * radius;
    }

    
    
    
    
    [[nodiscard]] bool passesAimGates(auto&& target, cs2::CUserCmd* cmd) const noexcept
    {
        const HitboxWants wants = wantedHitboxes();
        int hitchanceThreshold = GET_CONFIG_VAR(triggerbot_vars::Hitchance);

        auto&& localPawn = hookContext.activeLocalPlayerPawn();
        const auto eye = localPawn.eyePosition();
        const UserCmd userCmd{cmd};
        const auto pitch = userCmd.viewPitch();
        const auto yaw = userCmd.viewYaw();
        if (!eye.hasValue() || !pitch.hasValue() || !yaw.hasValue())
            return true;

        // gate on the hitbox under the crosshair; hitchance must sample what
        // the crosshair actually points at, not the primary group's hitbox.
        const auto matched = onSelectedBones(target, eye.value(), pitch.value(), yaw.value(), wants);
        if (!matched.hasValue()) {
            return false;
        }

        if (hitchanceThreshold > 0) {
            if (!passesHitchanceOn(localPawn, eye.value(), matched.value(), hitchanceThreshold)) {
                return false;
            }
        }

        return true;
    }

    
    
    
    
    
    
    
    
    
    
    [[nodiscard]] bool passesSeededFire(auto&& target, cs2::CUserCmd* cmd) const noexcept
    {
        if (!GET_CONFIG_VAR(triggerbot_vars::SeededFire) || GET_CONFIG_VAR(triggerbot_vars::SpreadCompensation))
            return true;

        auto&& localPawn = hookContext.activeLocalPlayerPawn();
        auto solver = hookContext.template make<SpreadSolver>();
        const auto params = solver.weaponParams(localPawn.getActiveWeapon());
        const auto tick = hookContext.localPlayerController().tickBase();
        const UserCmd userCmd{cmd};
        const auto pitch = userCmd.viewPitch();
        const auto yaw = userCmd.viewYaw();
        const auto punch = localPawn.aimPunchAngle();
        const auto eye = localPawn.eyePosition();
        if (!params.hasValue() || !tick.hasValue() || !pitch.hasValue() || !yaw.hasValue() || !eye.hasValue())
            return true;

        float punchPitch = 0.0f, punchYaw = 0.0f;
        if (punch.hasValue()) {
            punchPitch = punch.value().x;
            punchYaw = punch.value().y;
        }
        const float writtenPitch = pitch.value() - punchPitch;
        const float writtenYaw = yaw.value() - punchYaw;

        const auto seed = solver.seed(typename SpreadSolver<HookContext>::Angles{writtenPitch, writtenYaw, 0.0f}, tick.value());
        if (!seed.hasValue())
            return true;
        const auto offset = solver.spreadOffset(seed.value(), params.value());
        if (!offset.hasValue()) return false;

        
        
        
        const auto basis = shot_geometry::angleVectors(writtenPitch, writtenYaw);
        const cs2::Vector direction = shot_geometry::normalized(cs2::Vector{
            basis.forward.x + basis.left.x * offset.value().x + basis.up.x * offset.value().y,
            basis.forward.y + basis.left.y * offset.value().x + basis.up.y * offset.value().y,
            basis.forward.z + basis.left.z * offset.value().x + basis.up.z * offset.value().y,
        });

        
        
        constexpr float kTraceRange = 8192.0f;
        const cs2::Vector end{eye.value().x + basis.forward.x * kTraceRange,
                              eye.value().y + basis.forward.y * kTraceRange,
                              eye.value().z + basis.forward.z * kTraceRange};
        const auto trace = Tracing::traceLine(eye.value(), end, static_cast<void*>(static_cast<cs2::C_BaseEntity*>(localPawn.baseEntity())));
        if (!trace.didHit)
            return true;

        return shot_geometry::rayReachesSphere(eye.value(), direction, trace.endPos, kSeededFireRadius);
    }

    
    
    
    
    [[nodiscard]] bool passesMaxAccuracyGate() const noexcept
    {
        if (!GET_CONFIG_VAR(triggerbot_vars::MaxAccuracyOnly))
            return true;
        const bool pass = hookContext.activeLocalPlayerPawn().isAtMaxAccuracy();
        return pass;
    }

    
    
    
    
    
    
    
    
    [[nodiscard]] bool passesVisibility(auto&& target, cs2::CUserCmd* cmd) const noexcept
    {
        const bool wallCheck = GET_CONFIG_VAR(triggerbot_vars::WallCheck);
        const int autowallThickness = GET_CONFIG_VAR(triggerbot_vars::Autowall) ? static_cast<int>(GET_CONFIG_VAR(triggerbot_vars::AutowallMaxThickness)) : 0;
        if (!wallCheck && autowallThickness <= 0)
            return true;

        auto&& localPawn = hookContext.activeLocalPlayerPawn();
        const auto eye = localPawn.eyePosition();
        if (!eye.hasValue())
            return false;

        const UserCmd command{cmd};
        const auto pitch = command.viewPitch();
        const auto yaw = command.viewYaw();
        if (!pitch.hasValue() || !yaw.hasValue()) return false;
        // Match the recoil correction performed by the later shot writer.
        const auto punch = localPawn.aimPunchAngle();
        if (!punch.hasValue()) return false;
        const auto shots = localPawn.shotsFired();
        const bool rcsApplied = GET_CONFIG_VAR(rcs_vars::Enabled) && shots.hasValue() && shots.value() >= 1;
        const bool writerCorrectsPunch = GET_CONFIG_VAR(triggerbot_vars::SpreadCompensation) && !rcsApplied;
        const auto direction = shot_geometry::angleVectors(pitch.value() + (writerCorrectsPunch ? 0.0f : punch.value().x),
            yaw.value() + (writerCorrectsPunch ? 0.0f : punch.value().y)).forward;

        void* const skip = static_cast<cs2::C_BaseEntity*>(localPawn.baseEntity());
        void* const targetEntity = static_cast<cs2::C_BaseEntity*>(target.baseEntity());

        auto&& weapon = localPawn.getActiveWeapon();
        const auto damage = weapon.baseDamage();
        const auto rangeModifier = weapon.rangeModifier();
        const auto maxRange = weapon.maxRange();
        if (!damage.hasValue() || !rangeModifier.hasValue() || !maxRange.hasValue())
            return false;
        if (!std::isfinite(maxRange.value()) || maxRange.value() <= 0) return false;
        const cs2::Vector end{eye.value().x + direction.x * maxRange.value(),
            eye.value().y + direction.y * maxRange.value(), eye.value().z + direction.z * maxRange.value()};
        if (autowallThickness <= 0) {
            const auto trace = Tracing::traceLine(eye.value(), end, skip, Autowall::kBulletMask);
            return trace.valid && trace.didHit && trace.hitEntity == targetEntity;
        }
        penetration::TraceBudget budget;
        const penetration::Limits limits{90.0f, float(autowallThickness), 4};
        const auto impact = Autowall::evaluate(eye.value(), end, skip, targetEntity,
            {damage.value(), weapon.penetrationPower().valueOr(0.0f), rangeModifier.value(), maxRange.value()},
            limits, budget, hookContext);
        if (!impact.hasValue() || !hitgroupWanted(impact.value().hitgroup, wantedHitboxes())) return false;
        const auto armorRatio = weapon.armorRatio();
        const auto headMultiplier = weapon.headshotMultiplier();
        if (!armorRatio.hasValue() || !headMultiplier.hasValue()) return false;
        // min-damage gate: the shot must leave the target at <= health - MinDamage.
        const float minDamage = static_cast<float>(GET_CONFIG_VAR(triggerbot_vars::MinDamage));
        return Autowall::healthDamage(hookContext, targetEntity, impact.value().damage, impact.value().hitgroup,
            armorRatio.value(), headMultiplier.value()).valueOr(0.0f) >= (minDamage > 1.0f ? minDamage : 1.0f);
    }


    // bone selection on the game's own hitbox set (same data the aimbot's
    // multipoint uses). bone 6 is the NECK; the head hitbox remaps to bone 7.
    [[nodiscard]] Optional<MatchedHitbox> onSelectedBones(auto&& target, const cs2::Vector& eye, float pitch, float yaw,
        const HitboxWants& wants) const noexcept
    {
        const auto forward = shot_geometry::angleVectors(pitch, yaw).forward;
        auto&& node = target.baseEntity().gameSceneNode();

        const auto set = Hitboxes::query(node.raw());
        if (set.count > 0) {
            for (int i = 0; i < set.count; ++i) {
                const auto& entry = set.entries[i];
                if (!hitgroupWanted(Hitboxes::hitgroupFromHitbox(entry.index), wants))
                    continue;
                const auto shape = worldHitboxShape(node, entry);
                if (shape.hasValue() && shape.value().intersects(eye, forward))
                    return MatchedHitbox{shape.value(), Hitboxes::hitgroupFromHitbox(entry.index), false};
            }
            return {};
        }

        // chain dead: sphere approximation fallback so the trigger degrades
        // instead of vanishing.
        static constexpr int kBones[]{kHeadBone, kChestBone, kStomachBone};
        static constexpr int kHitgroups[]{1, 2, 3};
        static constexpr float kRadii[]{kHeadRadius, kBodyRadius, kBodyRadius};
        const bool want[3] = {wants.head, wants.chest, wants.stomach};
        for (int i = 0; i < 3; ++i) {
            if (!want[i])
                continue;
            const auto bone = node.bonePosition(kBones[i]);
            if (!bone.hasValue())
                continue;
            if (shot_geometry::rayReachesSphere(eye, forward, bone.value(), kRadii[i]))
                return MatchedHitbox{hitbox_geometry::Shape{}, kHitgroups[i], true, bone.value(), kRadii[i]};
        }
        return {};
    }

    // world-space shape of one hitbox entry: bone transform from the live
    // skeleton cache + the hitbox's own extents.
    [[nodiscard]] Optional<hitbox_geometry::Shape> worldHitboxShape(auto&& node, const Hitboxes::Entry& entry) const noexcept
    {
        const auto transform = node.boneTransform(entry.bone);
        if (!transform.hasValue())
            return {};
        const auto shape = hitbox_geometry::from(entry,
            transform.value().position, transform.value().rotation, transform.value().scale);
        if (!shape.valid)
            return {};
        return shape;
    }

    
    
    
    
    // hitchance sampled against the hitbox under the crosshair (the true
    // capsule/box shape): threshold% of the spread cone's samples must land
    // inside what we are about to hit.
    [[nodiscard]] bool passesHitchanceOn(auto&& localPawn, const cs2::Vector& eye,
        const MatchedHitbox& matched, int thresholdPercent) const noexcept
    {
        auto solver = hookContext.template make<SpreadSolver>();
        const auto params = solver.weaponParams(localPawn.getActiveWeapon());
        if (!params.hasValue())
            return false;

        const cs2::Vector aimPoint = matched.fromFallback ? matched.fallbackAim : matched.shape.center();
        const float radius = matched.fromFallback ? matched.fallbackRadius : matched.shape.radius;

        // sample the spread cone in a basis oriented at the aim point
        // (matches the shot writer's spread model).
        const auto aimAngles = shot_geometry::anglesTo(eye, aimPoint);
        const auto basis = shot_geometry::angleVectors(aimAngles.pitch, aimAngles.yaw);

        int hits = 0;
        for (int sample = 0; sample < kHitchanceSamples; ++sample) {
            const auto spread = solver.estimatedSpreadOffset(static_cast<std::uint32_t>(sample), params.value());
            if (!spread.hasValue()) return false;
            const cs2::Vector direction = shot_geometry::normalized(cs2::Vector{
                basis.forward.x + basis.left.x * spread.value().x + basis.up.x * spread.value().y,
                basis.forward.y + basis.left.y * spread.value().x + basis.up.y * spread.value().y,
                basis.forward.z + basis.left.z * spread.value().x + basis.up.z * spread.value().y,
            });
            if (shot_geometry::rayReachesSphere(eye, direction, aimPoint, radius))
                ++hits;
        }

        return (hits * 100 / kHitchanceSamples) >= thresholdPercent;
    }

    
    
    [[nodiscard]] Optional<int> crosshairEntityIndex(auto&& localPawn) const noexcept
    {
        const auto offset = hookContext.schemaSystem().getFieldOffset("C_CSPlayerPawn", "m_iIDEntIndex");
        if (!offset.has_value() || *offset <= 0)
            return {};

        int index{};
        std::memcpy(&index, reinterpret_cast<const std::byte*>(static_cast<cs2::C_BaseEntity*>(localPawn.baseEntity())) + *offset, sizeof(index));
        if (index <= 0 || index >= 32768)
            return {};
        return index;
    }

    
    
    
    
    
    [[nodiscard]] float delaySeconds() const noexcept
    {
        const auto a = static_cast<std::uint8_t>(GET_CONFIG_VAR(triggerbot_vars::DelayMilliseconds));
        const auto b = static_cast<std::uint8_t>(GET_CONFIG_VAR(triggerbot_vars::DelayMillisecondsMax));
        const auto lo = a < b ? a : b;
        const auto hi = a < b ? b : a;
        const auto span = static_cast<std::uint32_t>(hi - lo) + 1u; 
        const auto chosen = static_cast<std::uint8_t>(lo + static_cast<std::uint8_t>(nextRandom() % span));
        return static_cast<float>(chosen) / 1000.0f;
    }

    // every rolled delay (reaction, refire, aim time) gets a random
    // 0.75..1.25 multiplier so the click rhythm isn't a metronome.
    [[nodiscard]] float humanizeDelay(float seconds) const noexcept
    {
        if (!GET_CONFIG_VAR(triggerbot_vars::HumanizeDelays) || seconds <= 0.0f)
            return seconds;
        constexpr std::uint32_t kJitterSteps = 51; // 0.75 .. 1.25 inclusive
        const float jitter = 0.75f + static_cast<float>(nextRandom() % kJitterSteps) / 100.0f;
        return seconds * jitter;
    }

    
    
    
    
    
    
    [[nodiscard]] std::uint64_t nextRandom() const noexcept
    {
        if (rngState == 0) {
            float seed = 1.0f;
            if (const auto now = hookContext.globalVars().curtime(); now.hasValue())
                seed = now.value();
            std::uint32_t seedBits{};
            std::memcpy(&seedBits, &seed, sizeof(seedBits));
            std::uint64_t z = (static_cast<std::uint64_t>(seedBits) << 1) | 1ull;
            z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
            z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
            rngState = z ^ (z >> 31);
            if (rngState == 0)
                rngState = 0x9E3779B97F4A7C15ull;
        }
        std::uint64_t x = rngState;
        x ^= x >> 12;
        x ^= x << 25;
        x ^= x >> 27;
        rngState = x;
        return x * 0x2545F4914F6CDD1Dull;
    }

    
    
    
    
    
    static constexpr int kHeadBone = 6;
    static constexpr int kChestBone = 4;
    static constexpr int kStomachBone = 2;
    static constexpr float kHeadRadius = 6.0f;
    static constexpr float kBodyRadius = 16.0f;
    static constexpr int kHitchanceSamples = 256;
    static constexpr float kScanFov = 3.0f;

    
    
    

    
    
    
    static constexpr float kSeededFireRadius = 6.0f;

    
    
    static void disarm() noexcept
    {
        armed = false;
        armedTarget = nullptr;
        fireAtTime = 0.0f;
        firedAtTarget = false;
    }

    
    
    

    
    inline static bool armed{false};
    inline static cs2::C_BaseEntity* armedTarget{nullptr};
    inline static float fireAtTime{0.0f};
    // set after a shot at the armed target; AutoRefire's refires skip the
    // accuracy gates.
    inline static bool firedAtTarget{false};

    
    
    inline static std::uint64_t rngState{0};

    HookContext& hookContext;
};
