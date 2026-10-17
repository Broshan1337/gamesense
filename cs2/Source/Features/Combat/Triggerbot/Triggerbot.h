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
        // Activation: Hold = active while the key is down; Toggle = a press
        // flips the latch (PanicKey's rising-edge shape). The latch update runs
        // every user command BEFORE any early-out so the edge detector never
        // misses a release, and can't be re-flipped by a mid-press panic: an
        // unbound key or the master toggle off drops the latch.
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
            // Breadcrumb (friend report "toggle ON on target, nothing happens"):
            // HOLD mode fires only while the key is DOWN (default middle click),
            // TOGGLE latches on press. This line names which one blocked.
            if (enabled)
                VerifyConsole::write(4.0f, "trig", "gate activation: mode=%s keyDown=%d latched=%d",
                    toggleMode ? "toggle" : "hold", keyDown ? 1 : 0,
                    triggerbot_state::latched ? 1 : 0);
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

        // Target selection, the reference legitbot's three routes in order: the direct
        // crosshair pawn, then (only when the crosshair found nothing) the through-wall
        // scan and the backtrack ghost scan. All three feed the same arm/delay/fire
        // machinery below; scan targets carry their own resolved point + hitgroup and
        // skip the direct-only gates.
        ScanTarget selection{};
        if (const auto direct = crosshairTarget()) {
            selection.pawn = direct.rawPawn();
        } else {
            const auto scanned = scanTarget(cmd);
            if (!scanned.hasValue()) {
                // Breadcrumb: name the crosshair index so "on target but no
                // fire" reports show whether the game trace even sees it.
                VerifyConsole::write(4.0f, "trig", "no target: crosshair idx=%d (scans off)",
                    crosshairEntityIndex(hookContext.activeLocalPlayerPawn()).valueOr(-99));
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
            // Aim time (friend request): the crosshair must HOLD on this target
            // for AimTimeMs before the reaction delay even begins to roll.
            const float aimTime = static_cast<float>(GET_CONFIG_VAR(triggerbot_vars::AimTimeMs)) / 1000.0f;
            const float delay = delaySeconds();
            fireAtTime = now.value() + humanizeDelay(aimTime + delay);
            VerifyConsole::write(4.0f, "trig", "armed (aim %.0f ms + humanized delay)",
                aimTime * 1000.0f);
            return;
        }

        if (now.value() < fireAtTime)
            return;

        // Rapid refire (friend request "automatic pistols"): the FIRST shot at a
        // target runs the full gates; shots after it just click - semi-autos keep
        // firing at weapon speed while held on target, no per-shot inaccuracy
        // stall (the "slow / sometimes not shooting" report).
        if (!GET_CONFIG_VAR(triggerbot_vars::AutoRefire) || !firedAtTarget) {
            if (!wouldShotLand(target)) {
                VerifyConsole::write(4.0f, "trig", "gate wouldShotLand");
                return;
            }
            if (!passesMaxAccuracyGate()) {
                VerifyConsole::write(4.0f, "trig", "gate maxAccuracy");
                return;
            }
        }
        if (selection.fromBacktrack) {
            // A backtrack ghost: the direct trace already failed by construction, so the
            // visibility/hitchance/seeded gates on the LIVE pose are the wrong question -
            // the record's own hitgroup carries the bone decision and the arm/delay
            // logic above already re-armed per target.
            if (!hitgroupWanted(selection.hitgroup, wantedHitboxes())) {
                VerifyConsole::write(4.0f, "trig", "gate backtrack hitgroup=%d", selection.hitgroup);
                return;
            }
        } else if (selection.throughWall) {
            // A through-wall target: the scan already damage-checked the bone point; this
            // re-checks the actual fire direction (the crosshair) penetrates for enough
            // damage - the shot the spread-compensated writer will actually send.
            if (!passesVisibility(target, cmd)) {
                VerifyConsole::write(4.0f, "trig", "gate throughwall visibility");
                return;
            }
            if (!hitgroupWanted(selection.hitgroup, wantedHitboxes())) {
                VerifyConsole::write(4.0f, "trig", "gate throughwall hitgroup=%d", selection.hitgroup);
                return;
            }
        } else {
            if (!passesVisibility(target, cmd)) {
                VerifyConsole::write(4.0f, "trig", "gate visibility (wall=%d autowall=%d)",
                    GET_CONFIG_VAR(triggerbot_vars::WallCheck) ? 1 : 0,
                    GET_CONFIG_VAR(triggerbot_vars::Autowall) ? 1 : 0);
                return;
            }
            if (!passesAimGates(target, cmd)) {
                VerifyConsole::write(4.0f, "trig", "gate aim (hitboxes h=%d c=%d s=%d a=%d l=%d hc=%d)",
                    GET_CONFIG_VAR(triggerbot_vars::HitHead) ? 1 : 0,
                    GET_CONFIG_VAR(triggerbot_vars::HitChest) ? 1 : 0,
                    GET_CONFIG_VAR(triggerbot_vars::HitStomach) ? 1 : 0,
                    GET_CONFIG_VAR(triggerbot_vars::HitArms) ? 1 : 0,
                    GET_CONFIG_VAR(triggerbot_vars::HitLegs) ? 1 : 0,
                    static_cast<int>(GET_CONFIG_VAR(triggerbot_vars::Hitchance)));
                return;
            }
            if (!passesSeededFire(target, cmd)) {
                VerifyConsole::write(4.0f, "trig", "gate seeded");
                return;
            }
        }

        // The shot goes out.
        if (!hookContext.template make<AttackCommand>().press(cmd))
            return;
        firedAtTarget = true;
        VerifyConsole::write(4.0f, "trig", "FIRE");
        // Refire pause (friend request "delay for pistols like Dual Berettas"):
        // space subsequent shots at the same target; the first shot above ran
        // after AimTime + reaction delay, this only paces the follow-ups.
        const float refireDelay = static_cast<float>(GET_CONFIG_VAR(triggerbot_vars::RefireDelayMs)) / 1000.0f;
        if (refireDelay > 0.0f)
            fireAtTime = now.value() + humanizeDelay(refireDelay);

        
        
        
        
        
        
        
        
        
        
        
        if (GET_CONFIG_VAR(triggerbot_vars::SpreadCompensation)) {
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
                                                                                   0.0f ,
                                                                                   true));
            }
        }
    }

    void onUnload() const noexcept
    {
        disarm();
    }

private:
    // A target resolved by one of the fallback scans (through-wall or backtrack) instead of
    // the direct crosshair trace. `point`/`hitgroup` are the scan's own resolution - the
    // reference legitbot's ScanTargetThroughWall / ScanBacktrackTarget results.
    struct ScanTarget {
        cs2::C_CSPlayerPawn* pawn{};
        cs2::Vector point{};
        int hitgroup{0};
        bool throughWall{false};
        bool fromBacktrack{false};
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

    // The real hitgroup mapping (the shared Hitboxes helper - Lagcomp records
    // and this gate agree by construction): 1 head, 2 chest, 3 stomach,
    // 4|5 arms, 6|7 legs, 8 gear not selectable.
    [[nodiscard]] static bool hitgroupWanted(int hitgroup, const HitboxWants& wants) noexcept
    {
        return Hitboxes::hitgroupWantedBySelection(hitgroup, wants.head, wants.chest, wants.stomach, wants.arms, wants.legs);
    }


    // The reference legitbot's fallback selection, tried in its order: the through-wall scan
    // (crosshair on world geometry - fire at the closest-to-crosshair enemy hitbox that
    // still takes >= MinDamage through the wall), then the backtrack ghost (the crosshair
    // crossing an enemy's past position - the server's lag compensation lands the shot).
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

            // The real hitbox chain: scan the game's own hitbox set (true
            // centers, true hitgroups) instead of guessed bone points - the
            // same data the aimbot uses. Falls back to the old bone loop when
            // the chain is stale so the scan degrades, not disappears.
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

            // The record's REAL hitbox shape (model extents + the record's own
            // bone snapshot): the crosshair ray must cross the true capsule,
            // not an invented radius sphere (2026-10-15 audit item).
            const auto record = lagcomp.bestRecord(static_cast<cs2::C_BaseEntity*>(enemy.baseEntity()), eye.value(), pitch.value(), yaw.value(),
                                                   wants.head, wants.chest, wants.stomach, wants.arms, wants.legs, maxTicks);
            if (!record.hasValue())
                return;
            if (!hitgroupWanted(record.value().hitgroup, wants))
                return;
            if (!record.value().shape.valid || !record.value().shape.intersects(eye.value(), direction))
                return;
            best = ScanTarget{enemy.rawPawn(), record.value().aimPoint, record.value().hitgroup, false, true};
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

        // Every selected hitgroup gates EXPLICITLY through the real hitbox
        // chain (2026-10-15 friend report: chest/stomach selected but only
        // head shots fired - the old bodyAllowed shortcut skipped the
        // per-hitbox test entirely). The crosshair ray must reach one of the
        // SELECTED hitboxes, whatever the hitchance setting.
        if (!onSelectedBones(target, eye.value(), pitch.value(), yaw.value(), wants))
            return false;

        if (hitchanceThreshold > 0 && !passesHitchance(target, localPawn, eye.value(), pitch.value(), yaw.value(), wants.head, hitchanceThreshold))
            return false;

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
        // The min-damage gate (the reference's triggerbot_md): the shot along the crosshair
        // must leave the target at <= (health - MinDamage), not merely deal any damage.
        const float minDamage = static_cast<float>(GET_CONFIG_VAR(triggerbot_vars::MinDamage));
        return Autowall::healthDamage(hookContext, targetEntity, impact.value().damage, impact.value().hitgroup,
            armorRatio.value(), headMultiplier.value()).valueOr(0.0f) >= (minDamage > 1.0f ? minDamage : 1.0f);
    }


    // Bone selection (friend request), on the REAL hitbox chain (2026-10-15
    // "hitbox system" audit): the game's own hitbox set with true capsules -
    // the same data the aimbot's multipoint uses, so Aim and Triggerbot agree.
    // The old hardcoded path gated on bone 6, which the live-verified chain
    // showed to be the NECK (the head hitbox remaps to bone 7) - the "only
    // shoots if crosshair is on one point of the head" report.
    [[nodiscard]] bool onSelectedBones(auto&& target, const cs2::Vector& eye, float pitch, float yaw,
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
                    return true;
            }
            return false;
        }

        // Chain dead (update drift class): telemetry + the old sphere
        // approximation so the trigger degrades instead of vanishing.
        VerifyConsole::write(4.0f, "trig", "hitbox chain dead (count=0), sphere fallback");
        static constexpr int kBones[]{kHeadBone, kChestBone, kStomachBone};
        static constexpr float kRadii[]{kHeadRadius, kBodyRadius, kBodyRadius};
        const bool want[3] = {wants.head, wants.chest, wants.stomach};
        for (int i = 0; i < 3; ++i) {
            if (!want[i])
                continue;
            const auto bone = node.bonePosition(kBones[i]);
            if (!bone.hasValue())
                continue;
            if (shot_geometry::rayReachesSphere(eye, forward, bone.value(), kRadii[i]))
                return true;
        }
        return false;
    }

    // World-space shape of one real hitbox entry: the bone transform (the
    // game's live interpolated skeleton cache) + the hitbox's own extents.
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

    
    
    
    
    // Hitchance is evaluated against the highest-priority SELECTED bone (head
    // if selected, else chest, else stomach) - matches which shot we'd send.
    [[nodiscard]] bool passesHitchance(auto&& target, auto&& localPawn, const cs2::Vector& eye, float pitch, float yaw, bool wantHead, int thresholdPercent) const noexcept
    {
        auto solver = hookContext.template make<SpreadSolver>();
        const auto params = solver.weaponParams(localPawn.getActiveWeapon());
        if (!params.hasValue())
            return false;

        auto&& node = target.baseEntity().gameSceneNode();
        cs2::Vector aimPoint{};
        float radius = kBodyRadius;
        bool resolved = false;

        // Primary target = the first REAL hitbox of the priority group (head if
        // selected, else chest, else stomach) - true center and true radius.
        const int primaryGroup = wantHead ? 1 : (GET_CONFIG_VAR(triggerbot_vars::HitChest) ? 2 : 3);
        const auto hitboxSet = Hitboxes::query(node.raw());
        for (int i = 0; i < hitboxSet.count; ++i) {
            if (Hitboxes::hitgroupFromHitbox(hitboxSet.entries[i].index) != primaryGroup)
                continue;
            const auto shape = worldHitboxShape(node, hitboxSet.entries[i]);
            if (shape.hasValue()) {
                aimPoint = shape.value().center();
                radius = shape.value().radius;
                resolved = true;
                break;
            }
        }
        if (!resolved) {
            // Chain stale: the old bone-point + guessed-radius approximation.
            const int primaryBone = wantHead ? kHeadBone : (GET_CONFIG_VAR(triggerbot_vars::HitChest) ? kChestBone : kStomachBone);
            const auto bone = node.bonePosition(primaryBone);
            if (!bone.hasValue())
                return false;
            aimPoint = bone.value();
            radius = wantHead ? kHeadRadius : kBodyRadius;
        }

        const auto basis = shot_geometry::angleVectors(pitch, yaw);

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

    // Delay humanizer (friend request): every rolled delay (reaction, refire
    // pause, aim time) gets a random 0.75..1.25 multiplier so the click rhythm
    // isn't a metronome. Off = the configured delays verbatim.
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
    // Set after a shot goes out at the armed target: AutoRefire's subsequent
    // shots skip the accuracy gates (see the fire block).
    inline static bool firedAtTarget{false};

    
    
    inline static std::uint64_t rngState{0};

    HookContext& hookContext;
};
