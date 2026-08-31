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
#include <Features/Combat/ShotGeometry.h>
#include <Features/Combat/SubtickShotWriter.h>
#include <Features/Combat/Triggerbot/TriggerbotConfigVariables.h>
#include <GameClient/Bind.h>
#include <GameClient/Entities/BaseEntity.h>
#include <GameClient/Entities/PlayerPawn.h>
#include <GameClient/EntitySystem/EntitySystem.h>
#include <GameClient/MouseState.h>
#include <GameClient/SpreadPrediction/SpreadSolver.h>
#include <GameClient/Tracing/Tracing.h>
#include <GameClient/UserCmd.h>
#include <HookContext/HookContextMacros.h>
#include <SDL/SdlFunctions.h>
#include <Utils/Optional.h>
#include <Utils/Trig.h>
#include <Utils/VerifyConsole.h>

// Fires the moment the crosshair is on an enemy, after a configurable reaction delay, for as long
// as the hold button (MOUSE5) is down.
//
// Two things make this much smaller than a typical triggerbot, and both are worth stating because
// they are the reason almost none of the usual machinery is here:
//
//   1. WE DO NOT DECIDE WHAT IS UNDER THE CROSSHAIR - the game already did. C_CSPlayerPawn carries
//      `m_iIDEntIndex`, the entity index the client resolves every frame to draw the name/health
//      readout over whoever you are looking at. That is exactly the question a triggerbot asks,
//      already answered, by the game's own trace, against the game's own hitboxes. Casting our own
//      ray would be a second, worse answer to a question that is not in dispute. -1 means nothing
//      is under the crosshair.
//
//   2. WE FIRE THROUGH THE COMMAND ITSELF - subtick only, no kbutton. The attack goes out as part of
//      THIS usercmd: IN_ATTACK carried in BOTH button banks of buttons_pb (held + changed). The
//      server fires that bank-only press through its own calculated-time fallback - measured
//      in-game on build 14177: the command this hook receives carries an EMPTY input_history (so
//      attack1_start_history_index has nothing to point at and the history-entry rewrite in
//      SubtickShotWriter cannot apply here; see AttackCommand.h for the measured details). The
//      fire shape is velocity-cs2's minus the index their Windows build has history for, spliced
//      at the WriteMoveCrc stage where the command is fully built and nothing downstream
//      overwrites it.
//
// The delay is measured in game time rather than wall clock because we count against the input
// samples the game takes, and game time is what those advance with.
template <typename HookContext>
class Triggerbot {
public:
    explicit Triggerbot(HookContext& hookContext) noexcept
        : hookContext{hookContext}
    {
    }

    // Unload-time wipe of every cross-command static (the curtime-rollback guard handles map
    // changes, this handles re-injection).
    static void disarmStatics() noexcept
    {
        armed = false;
        fireAtTime = 0.0f;
    }

    // Fires through the command itself (see point 2 above): banks + buttons_pb plus the attack1
    // history index - all on THIS tick's command via AttackCommand. There is no persistent button
    // state any more, so every idle path below simply does nothing: there is nothing left pressed
    // that would need releasing.
    void onWriteMoveCrc(cs2::CUserCmd* cmd) const noexcept
    {
        if (!GET_CONFIG_VAR(triggerbot_vars::Enabled) || !Bind::isDown(GET_CONFIG_VAR(triggerbot_vars::HoldKey))) {
            disarm();
            return;
        }

        const auto now = hookContext.globalVars().curtime();
        if (!now.hasValue()) {
            disarm();
            return;
        }

        // Map change / reconnect rewinds curtime (server time restarts at 0). armed/fireAtTime are
        // static, so without this a stale fireAtTime from the old world either never fires or fires
        // instantly on the new one. Only ROLLBACKS count - curtime briefly stalling is normal.
        static float lastSeenCurtime = -1.0f;
        if (lastSeenCurtime > 0.0f && now.value() < lastSeenCurtime) {
            armed = false;
            fireAtTime = 0.0f;
        }
        lastSeenCurtime = now.value();

        auto&& target = crosshairTarget(cmd);
        if (!target) {
            disarm();
            return;
        }

        // First frame on a target only starts the clock; nothing fires until the delay elapses.
        if (!armed) {
            armed = true;
            fireAtTime = now.value() + delaySeconds();
            return;
        }

        if (now.value() < fireAtTime)
            return;

        // Accuracy gates: past the delay, but hold fire until the shot is worth taking - the simple
        // geometric cone check (AccuracyCheck), the head-only check, and the Monte-Carlo hitchance,
        // each a no-op unless its config is on. Stays armed so it fires the instant the shot becomes
        // worth taking.
        if (!wouldShotLand(target) || !passesMaxAccuracyGate() || !passesVisibility(target) || !passesAimGates(target, cmd) || !passesSeededFire(target, cmd))
            return;

        // On target, past the delay, accurate enough: fire THIS command. Stops again the moment the
        // crosshair leaves the target or the feature/hold-key is let go, above.
        if (!hookContext.template make<AttackCommand>().press(cmd))
            return;

        // Silent spread + punch compensation (see SubtickShotWriter): rewrite this command's
        // input_history view samples so the fired bullet lands ON the crosshair instead of wherever
        // the cone scatter carries it. Aim direction = the crosshair itself (the command's view
        // angles); the punch subtraction keeps a held spray on target as the view kicks. Runs at this
        // hook because the command is fully built here - and per tick, i.e. for EVERY bullet of a
        // spray, with the weapon's then-current recoil index / inaccuracy.
        //
        // Measured build 14177 caveat: this hook's command carries NO input_history (see
        // AttackCommand.h), so the writer early-outs and this compensation is dormant here - the
        // AccuracyCheck / hitchance gates above are what hold moving shots. The rage aimbot's
        // CreateMove-time writer is unaffected: its command does see the history.
        if (GET_CONFIG_VAR(triggerbot_vars::SpreadCompensation)) {
            auto&& localPawn = hookContext.activeLocalPlayerPawn();
            const UserCmd firedCmd{cmd};
            const auto aimPitch = firedCmd.viewPitch();
            const auto aimYaw = firedCmd.viewYaw();
            if (localPawn && aimPitch.hasValue() && aimYaw.hasValue()) {
                float punchPitch = 0.0f, punchYaw = 0.0f;
                if (const auto punch = localPawn.aimPunchAngle(); punch.hasValue()) {
                    punchPitch = punch.value().x;
                    punchYaw = punch.value().y;
                }
                // Return value deliberately ignored: the triggerbot's own accuracy/hitchance gates
                // already decided to fire - the writer's "did the correction exist" verdict is only
                // consumed by the rage aimbot's spread gate.
                static_cast<void>(hookContext.template make<SubtickShotWriter>().run(cmd, localPawn,
                                                                                   aimPitch.value(), aimYaw.value(),
                                                                                   punchPitch, punchYaw,
                                                                                   0.0f /* no backtrack rewind */,
                                                                                   true));
            }
        }
    }

    void onUnload() const noexcept
    {
        disarm();
    }

private:
    // The enemy player pawn under the crosshair, or a null-wrapped pawn (falsy) if there is nothing
    // shootable there. Returning the pawn rather than a bool lets the accuracy gate measure its range.
    [[nodiscard]] PlayerPawn<HookContext> crosshairTarget(cs2::CUserCmd* cmd) const noexcept
    {
        const auto none = hookContext.template make<PlayerPawn>(static_cast<cs2::C_CSPlayerPawn*>(nullptr));

        auto&& localPawn = hookContext.localPlayerController().pawn().template as<PlayerPawn>();
        if (!localPawn)
            return none;

        if (localPawn.isAlive() != true)
            return none;

        // Guns only (the reference triggerbot skips knife / taser / grenades / C4): a weapon whose
        // VData reports zero bullets cannot fire one, so pressing attack just swings or stabs.
        // Unresolved VData fails open - the fresh-weapon edge case, not worth blinding the bot over.
        const auto bullets = localPawn.getActiveWeapon().numBullets();
        if (bullets.hasValue() && bullets.value() <= 0)
            return none;

        const auto targetIndex = crosshairEntityIndex(localPawn);
        if (!targetIndex.hasValue())
            return none;

        auto* const entity = hookContext.template make<EntitySystem>().getEntityFromIndex(cs2::CEntityIndex{targetIndex.value()});
        if (!entity)
            return none;

        // m_iIDEntIndex is whatever the crosshair is over, which includes hostages, doors and
        // breakables. Only a player pawn is a target.
        auto&& baseEntity = hookContext.template make<BaseEntity>(static_cast<cs2::C_BaseEntity*>(entity));
        if (!baseEntity.classify().template is<cs2::C_CSPlayerPawn>())
            return none;

        auto&& target = baseEntity.template as<PlayerPawn>();
        if (!target || target.isControlledByLocalPlayer())
            return none;

        // isEnemy() already folds in the free-for-all case via teammatesAreEnemies(), so this is
        // both the team check and the deathmatch exception in one.
        if (target.isEnemy() != true)
            return none;

        const auto health = target.health();
        if (!health.hasValue() || health.value() <= 0)
            return none;

        // Spawn protection - the shot would not register, so taking it only gives the position away.
        if (const auto immune = target.hasImmunity(); immune.hasValue() && immune.value())
            return none;

        // STRICT CROSSHAIR CONFIRMATION. m_iIDEntIndex carries a forgiveness radius: the game flags
        // you as "looking at" a pawn while the view ray only passes NEAR their hull. Firing on that
        // alone just sprays compensated-but-off-target bullets - flash and sound, no damage, which
        // reads exactly like fake bullets. Verify with our own exact eye-ray along the COMMAND's
        // view angles (the angles this tick's bullet actually uses): the first thing the ray hits
        // must be the target itself. A wall, another player, or nothing (= ray past their hull)
        // all mean "not on target".
        const UserCmd userCmd{cmd};
        const auto aimPitch = userCmd.viewPitch();
        const auto aimYaw = userCmd.viewYaw();
        const auto eye = localPawn.eyePosition();
        if (!aimPitch.hasValue() || !aimYaw.hasValue() || !eye.hasValue())
            return none;

        const auto basis = shot_geometry::angleVectors(aimPitch.value(), aimYaw.value());
        constexpr float kTraceRange = 8192.0f;
        const cs2::Vector end{eye.value().x + basis.forward.x * kTraceRange,
                              eye.value().y + basis.forward.y * kTraceRange,
                              eye.value().z + basis.forward.z * kTraceRange};
        const auto trace = Tracing::traceLine(eye.value(), end, static_cast<void*>(static_cast<cs2::C_BaseEntity*>(localPawn.baseEntity())));
        if (!trace.didHit || trace.hitEntity != static_cast<void*>(static_cast<cs2::C_BaseEntity*>(target.baseEntity())))
            return none;

        return target;
    }

    // True if the shot is worth taking: the spread cone is small enough that the bullet lands on the
    // target. The cone (GetInaccuracy+GetSpread, via localPlayerBulletInaccuracy) is a tangent slope,
    // so cone * distance is the worst-case lateral miss in world units; if that is within the
    // configured radius the bullet cannot miss the target's body. A no-op (always true) unless the
    // AccuracyCheck config is on, and it fails OPEN - if the cone or a position can't be read we do
    // not block the shot, so a resolve failure never silently disables the triggerbot.
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

        // Compare squared to avoid a square root (there is no libm here): the shot lands if
        // (cone * distance)^2 <= radius^2.
        const auto radius = static_cast<float>(GET_CONFIG_VAR(triggerbot_vars::AccuracyRadius));
        const auto maxMissSquared = cone.value() * cone.value() * distanceSquared;
        return maxMissSquared <= radius * radius;
    }

    // The head-only and hitchance gates, both no-ops unless their config is on. Returns false to hold
    // fire. Needs the shot geometry - the local eye and the view angles this command carries - which it
    // reads once and shares. Fails OPEN: if the eye or view angles can't be read, it does not block the
    // shot, so a resolve failure never silently disables the triggerbot.
    [[nodiscard]] bool passesAimGates(auto&& target, cs2::CUserCmd* cmd) const noexcept
    {
        const bool headOnly = GET_CONFIG_VAR(triggerbot_vars::HeadOnly);
        int hitchanceThreshold = GET_CONFIG_VAR(triggerbot_vars::Hitchance);

        // Head-only MEANS "only fire when the shot will actually hit the head", so it must predict the
        // spread, not just check the crosshair ray. While moving, the crosshair can sit on the head yet
        // the cone sprays off - the "fires but misses while moving" case. Enforcing a head-hitchance
        // floor here makes head-only hold until enough of the predicted cone lands on the head (i.e. once
        // you counter-strafe / slow enough for the head to be hittable). The user's Hitchance slider only
        // raises the bar; it never drops below this floor while head-only is on.
        if (headOnly && hitchanceThreshold < kHeadOnlyMinHitchance)
            hitchanceThreshold = kHeadOnlyMinHitchance;

        if (!headOnly && hitchanceThreshold == 0)
            return true;

        auto&& localPawn = hookContext.activeLocalPlayerPawn();
        const auto eye = localPawn.eyePosition();
        const UserCmd userCmd{cmd};
        const auto pitch = userCmd.viewPitch();
        const auto yaw = userCmd.viewYaw();
        if (!eye.hasValue() || !pitch.hasValue() || !yaw.hasValue())
            return true;

        if (headOnly && !onHead(target, eye.value(), pitch.value(), yaw.value()))
            return false;

        if (hitchanceThreshold > 0 && !passesHitchance(target, localPawn, eye.value(), pitch.value(), yaw.value(), headOnly, hitchanceThreshold))
            return false;

        return true;
    }

    // SEED-MODE gate (velocity's give_me_your_seed, triggerbot form). The shot's spread seed is
    // hash(round0.5(written angles), tick) - both known on THIS command before it leaves, and the
    // tick rolls every command, so the deflection is a new dice roll per tick. With SpreadCompensation
    // OFF (the bullet leaves on the raw cone) this gate samples the one seed the shot will actually
    // use and holds fire on ticks whose predicted deflection carries the bullet off the crosshair
    // impact point - the trigger only eats the lucky ticks. A no-op unless SeededFire is on, and a
    // no-op while SpreadCompensation is on (the writer already cancels the cone exactly - there is
    // no luck left to wait for). The written angles mirror the writer's convention (aim minus the
    // CURRENT punch), and the seed tick is the same predicted server tick the writer stamps.
    // Fails OPEN on unreadable inputs: a resolve failure never silently disables the triggerbot.
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

        // Deflected direction: the shot's forward plus the tangent-plane offset in the (left, up)
        // basis of the written angles - the same composition hitchanceFraction uses, but for the
        // ONE deterministic seed this command carries instead of a Monte-Carlo sample.
        const auto basis = shot_geometry::angleVectors(writtenPitch, writtenYaw);
        const cs2::Vector direction = shot_geometry::normalized(cs2::Vector{
            basis.forward.x + basis.left.x * offset.x + basis.up.x * offset.y,
            basis.forward.y + basis.left.y * offset.x + basis.up.y * offset.y,
            basis.forward.z + basis.left.z * offset.x + basis.up.z * offset.y,
        });

        // Target point = the crosshair impact on the enemy (the shot is a crosshair shot; the
        // deflected one must still land on that spot for the tick to be worth firing).
        constexpr float kTraceRange = 8192.0f;
        const cs2::Vector end{eye.value().x + basis.forward.x * kTraceRange,
                              eye.value().y + basis.forward.y * kTraceRange,
                              eye.value().z + basis.forward.z * kTraceRange};
        const auto trace = Tracing::traceLine(eye.value(), end, static_cast<void*>(static_cast<cs2::C_BaseEntity*>(localPawn.baseEntity())));
        if (!trace.didHit)
            return true;

        return shot_geometry::rayReachesSphere(eye.value(), direction, trace.endPos, kSeededFireRadius);
    }

    // "Only fire at minimum inaccuracy" gate (velocity-cs2's is_max_accuracy). A no-op (returns true)    // unless the MaxAccuracyOnly config is on. Unlike the other gates this one fails CLOSED: it is an
    // opt-in restriction, so if the weapon/pawn state can't be read we hold fire rather than silently
    // firing while not actually at max accuracy (a gate that silently does nothing is worse than one
    // that is briefly over-strict, and the user can simply turn it off). See BaseWeapon::isMaxAccuracy.
    [[nodiscard]] bool passesMaxAccuracyGate() const noexcept
    {
        if (!GET_CONFIG_VAR(triggerbot_vars::MaxAccuracyOnly))
            return true;
        const bool pass = hookContext.activeLocalPlayerPawn().isAtMaxAccuracy();
        return pass;
    }

    // Visibility + autowall gate (the consumers of the CS2 trace primitive). WallCheck holds fire unless a
    // world trace from the local eye to the target's head (falling back to chest) reaches the target with
    // no wall in between. Autowall relaxes that: if a WORLD wall blocks the shot but is no thicker than
    // AutowallMaxThickness, the shot is taken anyway - the game's own FireBullet does the real penetration
    // and damage, so we only decide whether to pull the trigger. A no-op unless one of the two is on, and
    // it fails OPEN (an unreadable eye/bone, or a trace that can't be set up, never blocks the shot) so a
    // resolve failure can't silently disable the triggerbot. The local pawn is skipped so the ray never
    // self-collides.
    [[nodiscard]] bool passesVisibility(auto&& target) const noexcept
    {
        const bool wallCheck = GET_CONFIG_VAR(triggerbot_vars::WallCheck);
        const int autowallThickness = GET_CONFIG_VAR(triggerbot_vars::Autowall) ? static_cast<int>(GET_CONFIG_VAR(triggerbot_vars::AutowallMaxThickness)) : 0;
        if (!wallCheck && autowallThickness <= 0)
            return true;

        auto&& localPawn = hookContext.activeLocalPlayerPawn();
        const auto eye = localPawn.eyePosition();
        if (!eye.hasValue())
            return true;

        auto&& node = target.baseEntity().gameSceneNode();
        auto bone = node.bonePosition(kHeadBone);
        if (!bone.hasValue())
            bone = node.bonePosition(kChestBone);
        if (!bone.hasValue())
            return true;

        void* const skip = static_cast<cs2::C_BaseEntity*>(localPawn.baseEntity());
        void* const targetEntity = static_cast<cs2::C_BaseEntity*>(target.baseEntity());

        const auto forward = Tracing::traceLine(eye.value(), bone.value(), skip);
        // Clear line of sight, or the trace reached/hit the target itself: fire.
        if (!forward.didHit || forward.hitEntity == targetEntity)
            return true;

        // Blocked. With no autowall allowance, hold.
        if (autowallThickness <= 0)
            return false;

        // Only penetrate WORLD geometry (a null hit entity), never shoot through a player.
        if (forward.hitEntity != nullptr)
            return false;

        // Measure the wall: tracing back from the target, its first hit is the wall's far (exit) face. The
        // gap between the near face (forward.endPos) and the far face is the wall thickness.
        const auto back = Tracing::traceLine(bone.value(), eye.value(), skip);
        if (!back.didHit)
            return false;

        const float dx = forward.endPos.x - back.endPos.x;
        const float dy = forward.endPos.y - back.endPos.y;
        const float dz = forward.endPos.z - back.endPos.z;
        const float thickness = trig::squareRoot(dx * dx + dy * dy + dz * dz);
        const bool pass = thickness <= static_cast<float>(autowallThickness);
        return pass;
    }

    // True if the crosshair (a ray from the eye along the view angles) passes within the head sphere.
    // Strict: if the head bone can't be resolved, head-only holds fire rather than guessing. The sphere
    // radius approximates the head hitbox - a coarser answer than the game's own trace, but it needs no
    // trace call and no hitbox data.
    [[nodiscard]] bool onHead(auto&& target, const cs2::Vector& eye, float pitch, float yaw) const noexcept
    {
        const auto head = target.baseEntity().gameSceneNode().bonePosition(kHeadBone);
        if (!head.hasValue())
            return false;

        const auto forward = shot_geometry::angleVectors(pitch, yaw).forward;
        return shot_geometry::rayReachesSphere(eye, forward, head.value(), kHeadRadius);
    }

    // Monte-Carlo hitchance: sample the weapon's real spread cone (the game's own seed+cone functions,
    // via SpreadSolver) and count how many of the deflected shots would reach the target sphere - the
    // head if head-only is on, otherwise the body. Fires only when that fraction meets the threshold.
    // Fails OPEN (returns true) if the weapon's spread inputs or the target bone can't be read.
    [[nodiscard]] bool passesHitchance(auto&& target, auto&& localPawn, const cs2::Vector& eye, float pitch, float yaw, bool headOnly, int thresholdPercent) const noexcept
    {
        auto solver = hookContext.template make<SpreadSolver>();
        const auto params = solver.weaponParams(localPawn.getActiveWeapon());
        if (!params.hasValue())
            return true;

        auto&& node = target.baseEntity().gameSceneNode();
        const auto bone = headOnly ? node.bonePosition(kHeadBone) : node.bonePosition(kChestBone);
        if (!bone.hasValue())
            return true;
        const auto radius = headOnly ? kHeadRadius : kBodyRadius;

        const auto basis = shot_geometry::angleVectors(pitch, yaw);

        int hits = 0;
        for (int sample = 0; sample < kHitchanceSamples; ++sample) {
            const auto spread = solver.spreadOffset(static_cast<std::uint32_t>(sample), params.value());
            const cs2::Vector direction = shot_geometry::normalized(cs2::Vector{
                basis.forward.x + basis.left.x * spread.x + basis.up.x * spread.y,
                basis.forward.y + basis.left.y * spread.x + basis.up.y * spread.y,
                basis.forward.z + basis.left.z * spread.x + basis.up.z * spread.y,
            });
            if (shot_geometry::rayReachesSphere(eye, direction, bone.value(), radius))
                ++hits;
        }

        return (hits * 100 / kHitchanceSamples) >= thresholdPercent;
    }

    // Resolved by name through the schema system rather than by byte pattern, so it survives a game
    // update that moves the field. -1 is the game's own "nothing under the crosshair".
    [[nodiscard]] Optional<int> crosshairEntityIndex(auto&& localPawn) const noexcept
    {
        const auto offset = hookContext.schemaSystem().getFieldOffset("C_CSPlayerPawn", "m_iIDEntIndex");
        if (!offset.has_value() || *offset <= 0)
            return {};

        int index{};
        std::memcpy(&index, reinterpret_cast<const std::byte*>(static_cast<cs2::C_BaseEntity*>(localPawn.baseEntity())) + *offset, sizeof(index));
        if (index < 0)
            return {};
        return index;
    }

    // Rolls a fresh reaction delay in [min, max] milliseconds. Called once per target acquisition
    // (from onWriteMoveCrc's arming branch), so every time the crosshair lands on a new enemy the
    // wait before the shot is a different number - a human's reaction time is never the same twice.
    // The two sliders can be dragged past each other, so sort the bounds here rather than trust their
    // order; equal bounds collapse to a fixed delay (span == 1 -> the modulo is always 0).
    [[nodiscard]] float delaySeconds() const noexcept
    {
        const auto a = static_cast<std::uint8_t>(GET_CONFIG_VAR(triggerbot_vars::DelayMilliseconds));
        const auto b = static_cast<std::uint8_t>(GET_CONFIG_VAR(triggerbot_vars::DelayMillisecondsMax));
        const auto lo = a < b ? a : b;
        const auto hi = a < b ? b : a;
        const auto span = static_cast<std::uint32_t>(hi - lo) + 1u; // inclusive of both ends
        const auto chosen = static_cast<std::uint8_t>(lo + static_cast<std::uint8_t>(nextRandom() % span));
        return static_cast<float>(chosen) / 1000.0f;
    }

    // Small xorshift64* PRNG. There is no <random> under -nostdlib, and a reaction delay needs no
    // crypto-grade randomness - only to not be the same number every shot. Seeded lazily from the game
    // clock the first time it is used: curtime is a float that has been advancing since map load, so
    // its bit pattern differs moment to moment; splitmix64-mixing turns that into a full 64-bit state,
    // and the guard keeps the state non-zero (a zero state makes xorshift emit only zeroes). const
    // because the state is a static - it is not part of the object.
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

    // Hitbox geometry for the head-only / hitchance gates. kHeadBone (6) is the confirmed CS2 head bone
    // (the aimbot aims at it); kChestBone (4) is the body target. The radii approximate the head and
    // torso hitboxes in world units - deliberately a bit generous so a real on-target shot is not
    // rejected; tune down if the gates feel too loose. kHitchanceSamples is the Monte-Carlo count (256,
    // matching the SpreadPredictor tests) - enough for a stable percentage without a heavy per-shot loop.
    static constexpr int kHeadBone = 6;
    static constexpr int kChestBone = 4;
    static constexpr float kHeadRadius = 6.0f;
    static constexpr float kBodyRadius = 16.0f;
    static constexpr int kHitchanceSamples = 256;

    // Minimum fraction (percent) of the predicted spread cone that must land on the head before head-only
    // will fire. This is what stops head-only from spraying while moving: it holds until the movement
    // inaccuracy has decayed enough (counter-strafe / slow down) that the head is genuinely hittable.
    static constexpr int kHeadOnlyMinHitchance = 40;

    // Seed mode: how close (units) the predicted deflected shot must land to the crosshair impact
    // point for the tick to fire - a head-sized sphere, so a "lucky" tick is one whose bullet
    // actually lands on what the crosshair is touching.
    static constexpr float kSeededFireRadius = 6.0f;

    // Clears the pending shot, so a disabled feature or a crosshair that has moved off the target
    // can never leave a shot queued to go off later.
    static void disarm() noexcept
    {
        armed = false;
        fireAtTime = 0.0f;
    }

    // Hold-to-fire. The toggle in the menu arms the feature; nothing happens until this is held, so
    // the crosshair can cross an enemy freely the rest of the time. The hold bind is configurable
    // (triggerbot_vars::HoldKey, GameClient/Bind.h list).

    // Constant-initialised and trivially destructible, so no __cxa_guard under -nostdlib.
    inline static bool armed{false};
    inline static float fireAtTime{0.0f};

    // PRNG state for the randomised reaction delay. Zero means "not seeded yet"; nextRandom() seeds it
    // lazily and never lets it return to zero.
    inline static std::uint64_t rngState{0};

    HookContext& hookContext;
};
