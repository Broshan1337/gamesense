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

// Silent aim assist. Runs in CreateMove (slot 26, after the original) - the stage where the command's
// input_history is actually populated (WriteMoveCrc, slot 7, saw it empty). When the player is holding
// fire AND the shot begins this tick (attack1_start_history_index >= 0), it redirects that ONE history
// entry's view angles onto the selected hitbox of the best enemy within FOV. The server traces the
// bullet along that entry, so the shot lands on the target - while the BASE view angles (the rendered
// crosshair) are left untouched. Not moving the camera is what makes it SILENT: we only change the
// angle the server shoots along, never where the player is looking.
//
// 
//
// Offsets found by in-game measurement (a temporary diagnostic that matched each candidate pointer's
// pitch/yaw against the real view): the input_history is the repeated field at cmd+48 (size) / cmd+56
// (Rep*, elements at Rep+8), and each CSGOInputHistoryEntry's view_angles CMsgQAngle pointer sits at
// entry + 0x18 (its pitch/yaw are the usual +24/+28). At history size 1, that pointer's angles matched
// the real view exactly, which is how it was confirmed.
template <typename HookContext>
class Aimbot {
public:
    explicit Aimbot(HookContext& hookContext) noexcept
        : hookContext{hookContext}
    {
    }

    void onCreateMove(cs2::CUserCmd* cmd) const noexcept
    {
        // A fresh decision every tick; consumed by this tick's onWriteMoveCrc.
        forceShotThisTick = false;

        if (!GET_CONFIG_VAR(aimbot_vars::Enabled))
            return;

        // Steer the shot when the player is firing OR force-shot may auto-fire this tick. Force-shot
        // (velocity's force_shot/force_shot_air) splices an attack into the outgoing command itself
        // (subtick path, see AttackCommand.h), so unlike the fire-triggered path it does not need the
        // player to hold the mouse. If neither is active there is nothing to redirect and no shot to
        // stage. The staged decision is consumed by onWriteMoveCrc below - slot 6 runs between the
        // two hooks and rebuilds buttons_pb/subtick_moves, so the attack splice must happen there.
        // Backtracking keeps a per-enemy bone-record history; it must run every tick to have records
        // ready - and so must extrapolation, which builds its FUTURE record on top of the same history.
        // The capture itself runs at frame stage 6 in EntryPoints.h (velocity's timing); it used to
        // ALSO run here, but running Lagcomp::run() while CreateMove/prediction is mid-flight mutates
        // globalvars/skeleton state twice per tick - prime suspect of the 2026-08-23 23:56 SEGV.
        const bool backtrackEnabled = GET_CONFIG_VAR(aimbot_vars::Backtrack);
        const bool extrapolateEnabled = GET_CONFIG_VAR(aimbot_vars::Extrapolate);

        const bool forceShotEnabled = GET_CONFIG_VAR(aimbot_vars::ForceShot) || GET_CONFIG_VAR(aimbot_vars::ForceShotAir);
        const bool userFiring = MouseState::isButtonDown(sdl3::mousebutton::kLeft);
        if (!userFiring && !forceShotEnabled)
            return;

        UserCmd userCmd{cmd};
        if (!userCmd)
            return;

        auto&& localPawn = hookContext.activeLocalPlayerPawn();
        if (!localPawn || localPawn.isAlive() != true)
            return;

        auto aimTarget = hookContext.template make<AimTarget>();
        const auto eye = aimTarget.eyePosition(localPawn);
        const auto currentPitch = userCmd.viewPitch();
        const auto currentYaw = userCmd.viewYaw();
        if (!eye.hasValue() || !currentPitch.hasValue() || !currentYaw.hasValue())
            return;

        // Target acceptance region: velocity's fixed max_fov by default; with SpreadCircleFov on, the
        // weapon's CURRENT spread circle instead - the GetInaccuracy+GetSpread cone slope converted to
        // a half-angle in degrees, so only targets the current cone can actually reach are considered.
        float maxFov = aimbot_params::kMaxFov;
        if (GET_CONFIG_VAR(aimbot_vars::SpreadCircleFov)) {
            if (const auto cone = hookContext.localPlayerBulletInaccuracy(); cone.hasValue() && cone.value() > 0.0f)
                maxFov = trig::arcTangent2(cone.value(), 1.0f) * trig::kRadiansToDegrees;
        }
        // NOTE: leading moving targets no longer happens here - it moved into the Lagcomp record flow
        // below (velocity's extrapolated-record fallback), which needs the chosen target first.
        // TARGET STICKINESS: while a held target is still alive/an enemy/in FOV, keep aiming at IT
        // instead of re-running the FOV-best selection - with many candidates in FOV the per-tick
        // re-selection flips the camera between them (the "screen rotates left and right" report).
        // The handle (index+serial) survives entity recycling; if the held target died or left FOV,
        // best() returns {} and we fall back to a fresh selection.
        cs2::C_BaseEntity* preferredTarget = nullptr;
        if (lastTargetHandleValue != 0) {
            if (auto* const instance = hookContext.template make<EntitySystem>().getEntityFromHandle(cs2::CEntityHandle{lastTargetHandleValue}))
                preferredTarget = static_cast<cs2::C_BaseEntity*>(instance);
        }

        auto aim = aimTarget.best(eye.value(), currentPitch.value(), currentYaw.value(), maxFov, hitboxFlags(), 0, nullptr, preferredTarget);
        if (!aim.hasValue() && preferredTarget) {
            lastTargetHandleValue = 0;   // held target gone - select fresh
            aim = aimTarget.best(eye.value(), currentPitch.value(), currentYaw.value(), maxFov, hitboxFlags());
        }

        // VISIBILITY-AWARE SELECTION (velocity's rage parity): with WallCheck/Autowall on, a blocked
        // FOV-best candidate must not blind the whole aimbot for the tick. Fall through to the
        // next-best candidate that CAN be hit - through a wall the simulated shot can penetrate for
        // MinDamage when Autowall is on (passesVisibility encodes that rule) - instead of firing at
        // nothing. Bounded retries: each pass excludes exactly the rejected entity.
        {
            int rejected = 0;
            while (aim.hasValue() && !passesVisibility(localPawn, eye.value(), aim.value()) && rejected < 4) {
                ++rejected;
                aim = aimTarget.best(eye.value(), currentPitch.value(), currentYaw.value(), maxFov, hitboxFlags(), 0, aim.value().entity);
            }
        }
        if (!aim.hasValue())
            return; // No shootable target in FOV - never auto-fire (forceShotThisTick is already false).

        // The raw hitbox angle is what we write if compensation is off or cannot be computed; roll
        // stays 0 in that case. When compensation succeeds it replaces all three with an angle that
        // cancels the shot's predicted spread (see SubtickShotWriter / SpreadSolver).
        // Multipoint refines the aim onto the best-hitchance point across the hitbox before we redirect.
        auto chosen = aim.value();
        if (GET_CONFIG_VAR(aimbot_vars::Multipoint))
            chosen = refineMultipoint(localPawn, eye.value(), chosen);

        // Final gate: multipoint's refinement is pure geometry and can slide the aimed point; make sure
        // the shot we are about to stage is still one the visibility rules allow.
        if (!passesVisibility(localPawn, eye.value(), chosen))
            return; // A rejected gate is like having no target this tick - nothing staged.

        // Backtracking: aim at a recent PAST position of this target (records + server tick stamped so
        // the server rewinds there). velocity's fallback: when the target has NO valid past record,
        // EXTRAPOLATE a future position instead and lead it. Both stamp their sim time into
        // input_history; whichever produced the shot's aim point wins.
        float backtrackSimTime = 0.0f;
        if (backtrackEnabled || extrapolateEnabled) {
            const auto flags = hitboxFlags();
            auto&& lagcomp = hookContext.template make<Lagcomp>();
            Optional<typename Lagcomp<HookContext>::Result> picked;
            if (backtrackEnabled) {
                picked = lagcomp.bestRecord(chosen.entity, eye.value(), chosen.angles.pitch, chosen.angles.yaw, flags.head, flags.chest, flags.stomach, flags.arms, flags.legs, static_cast<int>(GET_CONFIG_VAR(aimbot_vars::BacktrackTicks)));
            }
            if (!picked.hasValue() && extrapolateEnabled) {
                picked = lagcomp.extrapolatedResult(chosen.entity, eye.value(), chosen.angles.pitch, chosen.angles.yaw, flags.head, flags.chest, flags.stomach, flags.arms, flags.legs, static_cast<int>(GET_CONFIG_VAR(aimbot_vars::ExtrapolateTicks)));
            }
            if (picked.hasValue()) {
                const auto angles = shot_geometry::anglesTo(eye.value(), picked.value().aimPoint);
                chosen.angles = typename AimTarget<HookContext>::Angles{angles.pitch, angles.yaw};
                chosen.aimPoint = picked.value().aimPoint;
                chosen.hitgroup = picked.value().hitgroup;

                // Only stamp a rewind the server can actually honor. Our stamp is recordTick+1
                // (velocity's convention, valid when the target's simTime lags the server by
                // latency); a record at or AHEAD of the server's current player tick would be
                // future-dated and the server just clamps it back ("Player tick from command
                // clamped by server" - measured on the zero-latency local server, where the
                // newest record IS the server tick). In that case the game's own player_tick_count
                // is already the correct tick, so we leave the entry untouched.
                const auto serverTick = hookContext.localPlayerController().tickBase();
                const int recordTick = static_cast<int>(picked.value().simulationTime / Lagcomp<HookContext>::kTickInterval);
                if (!serverTick.hasValue() || recordTick < serverTick.value())
                    backtrackSimTime = picked.value().simulationTime;
            }
        }

        // velocity's autostop: while firing at this target, counter-strafe to a stop so the shot's
        // inaccuracy collapses (see AutoStop config comment for why moving shots scatter otherwise).
        if (GET_CONFIG_VAR(aimbot_vars::AutoStop))
            autoStop(localPawn, currentYaw.value(), userCmd);

        // Recoil (aim punch): the game fires along (written angle + aim punch), so subtracting the
        // current punch makes the shot land where we aimed as the view kicks up during a spray.
        // Applied per-entry inside the writer below, AFTER that entry's spread correction - matching
        // velocity-cs2 (which writes corrected_aim - aim_punch); roll is left as the correction set it.
        float punchPitch = 0.0f, punchYaw = 0.0f;
        if (GET_CONFIG_VAR(aimbot_vars::RecoilCompensation)) {
            if (const auto punch = localPawn.aimPunchAngle(); punch.hasValue()) {
                punchPitch = punch.value().x;
                punchYaw = punch.value().y;
            }
        }

        // Stage the shot for WriteMoveCrc instead of writing it here. MEASURED on this build
        // (slot ordering + Rep realloc at 0x18eb964): slot 6 (BuildUserCmd) runs BETWEEN this hook
        // and slot 7 and REBUILDS input_history from scratch - any entry edited here never reaches
        // the wire, which is exactly the "first bullet of the click hits (raw crosshair shot), the
        // rest of the spray goes anywhere" symptom. Slot 7's command carries no history, so the
        // writer's base-view-angle path is what actually reaches the server: the shot fires from
        // the command's base angles, aimed at the target with spread+punch canceled. NON-SILENT
        // (velocity's silent=off equivalent): the camera visibly snaps onto the target while
        // firing - the accepted tradeoff until history-entry creation at slot 7 is implemented.
        // Spread correction computed HERE (CreateMove), not in the slot-7 writer: the FVA chains
        // staged this tick rotate toward the armed target, and the server blends the shot ACROSS
        // history entries - a chain rotating toward the raw angle dilutes an entry carrying a
        // corrected one (the ONE-ENTRY-RULE blend). Arming the chains with the SAME corrected
        // angle the entry will carry makes every sample agree. Backtracking keeps the old flow:
        // its correction belongs to the rewind tick and is solved in the writer.
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

        // FVA chains: while a silent spray keeps redirecting shots, arm each tick's history-chain
        // endpoint onto THIS staged aim direction. The chains are published later in the same
        // CreateMove pass (FvaEmulator runs after every angle-writer on purpose), so the server's
        // rewind walk finds a smooth rotation INTO the aim instead of a teleport onto it. The arm
        // is one-shot per published chain; consecutive spray ticks re-arm here like a heartbeat,
        // and the endpoint never touches the base message - rendering stays authoritative.
        if (stagedCorrectionValid)
            fva::setTargetAngle(stagedCorrectionAngles.pitch, stagedCorrectionAngles.yaw);
        else
            fva::setTargetAngle(chosen.angles.pitch, chosen.angles.yaw);

        // Force shot: with a target in FOV and the shot redirected onto it, auto-fire the instant the
        // weapon is at minimum inaccuracy for the allowed stance. Only the DECISION happens here - it
        // is staged and consumed by onWriteMoveCrc below, which splices the attack into the outgoing
        // command on the subtick path (see AttackCommand.h); slot 6 runs between the two hooks and
        // would overwrite anything pressed now. With force-shot off nothing is staged and the user's
        // own click still fires untouched.
        const bool forceDown = forceShotEnabled && shouldForceShoot(localPawn, eye.value(), chosen);
        forceShotThisTick = forceDown;
    }

    // Consumes the shot staged during CreateMove AND the force-shot decision.
    //
    // This must happen here and not in onCreateMove for two MEASURED reasons:
    //   1. slot 6 (BuildUserCmd) sits between the two hooks and rebuilds buttons_pb, subtick_moves
    //      AND input_history from the raw input state - anything written at CreateMove is gone.
    //   2. the command at THIS hook is the one that goes on the wire (slot 7 pre-original: our
    //      base-angle edit is covered by the recomputed move_crc).
    // SubtickShotWriter::run self-selects its path: history present (other builds/configs) =
    // per-entry silent redirect; empty history (this build) = base-view-angle write, which the
    // server falls back to. Either way the bullet leaves along the target angle with spread and
    // punch canceled.
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
                // THE ATTACK MARKER. attack1_start_history_index is what makes the server resolve a
                // shot along a history entry at all - and on this build it reaches us as -1 (slot 6
                // rebuilds the history empty, so the game clamps the index it wrote at CreateMove).
                // Without this write a MANUAL click (no AttackCommand::press runs for it) leaves the
                // marker at -1 and the server takes its no-history fallback: it fires along the BASE
                // view angles - the crosshair - and the entry we just redirected is never consulted
                // ("silent aim shots go where I am pointing"). Pointing the marker at the redirected
                // entry makes the same click resolve along the silent entry instead. Guarded by the
                // setter (only writes over -1), so a real game-written index always wins, and the
                // force-shot press below is a no-op after us. Only when the shot is not gate-held.
                // Seed-mode arm (velocity's give_me_your_seed, rage form): when the exact spread
                // correction is missing (wide cone), the gate normally holds fire - but the shot's
                // seed is derived from the angles THIS command is about to carry, so it can be
                // sampled first. A tick whose predicted deflection still lands on the aimed hitbox
                // fires even without a correction: exact-corrected shots, lucky-seed shots, and
                // nothing else. Only for live shots (the backtrack stamp resolves a different tick).
                const bool seedLucky = !lands
                    && GET_CONFIG_VAR(aimbot_vars::SeedFallback)
                    && stagedBacktrackSimTime <= 0.0f
                    && seedLandsOnTarget(localPawn);

                if (!lands && !seedLucky && GET_CONFIG_VAR(aimbot_vars::SpreadGate)) {
                    gateHeldFire = true;
                    const UserCmd userCmd{cmd};
                    userCmd.suppressAttack(cs2::CCSGOInput::Buttons::kAttack);
                    SubtickMoves<HookContext>::releaseButton(userCmd.baseMessage(), cs2::CCSGOInput::Buttons::kAttack);
                    // WHY did the solver fail? The weapon's live spread inputs decide the search
                    // window - logging them on every held tick says whether the cone is huge
                    // (moving/spraying: expected to hold fire) or tiny (standing: a solver bug).
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
                    // FORCE, not the guarded setter: on a real manual click the game has usually
                    // already written its own a1 (pointing at ITS newest entry - the FVA chain tail
                    // carrying the raw crosshair angles), and the guarded write would refuse -
                    // the server then fires along the game's entry ("first shots miss, hits only
                    // after more clicks"). We claimed the shot's entry THIS tick, so the marker is
                    // ours to own; the measured logs confirm only marker-written ticks hit.
                    const UserCmd userCmd{cmd};
                    if (userCmd.forceAttack1StartHistoryIndex(redirectedEntry))
                        // Own tag: the fast-path/entry-path lines use "hist" and are rate-limited -
                        // a same-second "hist" write used to swallow this line, making fired ticks
                        // LOOK like unmarked (missed) ones in the logs.
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

    // Seed-mode check for the spread gate's fallback arm: would the RAW shot (no correction - the
    // entry carries the aim minus the current punch) still land on the aimed hitbox with the seed
    // THIS command will produce? Mirrors the writer's angle convention exactly: entry angles =
    // aim - punch, seed tick = the predicted server tick, deflection applied in the written
    // angles' tangent basis. False on any unreadable input - a resolve failure must not wave a
    // bad shot through the gate.
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

    // No persistent input state exists any more (each command carries its own complete press/release),
    // so unloading just drops a staged decision.
    void onUnload() const noexcept
    {
        forceShotThisTick = false;
        shotStagedThisTick = false;
        lastTargetHandleValue = 0;
    }

private:
    // velocity-cs2's rage fire decision, compressed: fire when the stance is allowed AND (the weapon is at
    // minimum inaccuracy OR the predicted hitchance to the aimed point clears the threshold). velocity's
    // `shot_viable = accurate || force`, with force = force_shot && max_acc and accurate = hitchance >=
    // needed. Hitchance 0 disables that arm, so force-shot then fires on max-accuracy only (its original
    // behavior). False if the ground state can't be read - never auto-fire on missing data.
    [[nodiscard]] bool shouldForceShoot(auto&& localPawn, const cs2::Vector& eye, const typename AimTarget<HookContext>::Target& target) const noexcept
    {
        const bool ground = GET_CONFIG_VAR(aimbot_vars::ForceShot);
        const bool air = GET_CONFIG_VAR(aimbot_vars::ForceShotAir);
        const auto onGround = localPawn.isOnGround();
        if (!onGround.hasValue())
            return false;
        if (!(onGround.value() ? ground : air))
            return false;

        // Min-damage filter (velocity gates candidates by min_damage): only auto-fire if the shot to the
        // aimed point would do at least this much. 0 = off.
        const int minDamage = GET_CONFIG_VAR(aimbot_vars::MinDamage);
        if (minDamage > 0 && estimatedDamage(localPawn, eye, target) < static_cast<float>(minDamage))
            return false;

        if (localPawn.isAtMaxAccuracy())
            return true;

        const int hitchance = GET_CONFIG_VAR(aimbot_vars::Hitchance);
        return hitchance > 0 && passesHitchance(localPawn, eye, target, hitchance);
    }

    // Monte-Carlo hitchance to the aimed point: sample the weapon's real spread cone (the game's own
    // seed+cone functions, via SpreadSolver) along the angle to the target and count how many deflected
    // shots land within a body-sized sphere of the aim point. Fails CLOSED (returns false) if the spread
    // inputs can't be read - a missing prediction should not manufacture an auto-shot; the max-accuracy
    // arm above still applies.
    [[nodiscard]] bool passesHitchance(auto&& localPawn, const cs2::Vector& eye, const typename AimTarget<HookContext>::Target& target, int thresholdPercent) const noexcept
    {
        const int fraction = hitchanceFraction(localPawn, eye, target, kHitchanceSamples);
        return fraction >= 0 && fraction >= thresholdPercent;
    }

    // The predicted hitchance to `target.aimPoint` as a percentage (0..100), or -1 if the weapon spread
    // inputs can't be read. `samples` trades accuracy for speed (multipoint uses a smaller count over many
    // candidate points). Monte-Carlo over the game's own seed+cone functions.
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

    // velocity-cs2's multipoint / dynamic_pointscale: instead of only the bone centre, try a ring of
    // points across the hitbox (in the shot's tangent plane) and aim at whichever has the highest
    // predicted hitchance. Helps when the centre sits at the edge of the settling spread cone. Returns the
    // original target unchanged if multipoint can't improve on the centre. Uses a reduced sample count per
    // candidate to keep the per-frame cost bounded (candidates x samples raycasts).
    // Monte-Carlo sampling noise: at 256 samples the hitchance fraction wobbles roughly +-3%,
    // which is exactly the band in which two multipoint points trade the win back and forth -
    // the tick-to-tick aim wobble. The incumbent slot gets a bonus of that size, so a challenger
    // has to be genuinely better, not luckier this tick. Bounded: a point that is truly ~7%
    // better still takes over.
    static constexpr int kMultipointStickyBonus = 6;

    [[nodiscard]] typename AimTarget<HookContext>::Target refineMultipoint(auto&& localPawn, const cs2::Vector& eye, const typename AimTarget<HookContext>::Target& target) const noexcept
    {
        const float radius = (target.hitgroup == 1) ? kMultipointHeadRadius : kMultipointBodyRadius;
        const auto basis = shot_geometry::angleVectors(target.angles.pitch, target.angles.yaw);

        // The contest resets whenever the target entity or hitbox group changes: different
        // geometry invalidates last tick's slot choice.
        const bool incumbent = stickyMultipointEntity == target.entity && stickyMultipointHitgroup == target.hitgroup;

        auto best = target;
        int bestSlot = 0; // 0 = bone centre, 1..4 = ring offsets below
        int bestFraction = hitchanceFraction(localPawn, eye, target, kMultipointSamples);
        if (incumbent && stickyMultipointSlot == bestSlot)
            bestFraction += kMultipointStickyBonus;

        // Four cardinal points on the hitbox ring, in the (left, up) tangent plane of the shot.
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
            int fraction = hitchanceFraction(localPawn, eye, candidate, kMultipointSamples);
            if (incumbent && stickyMultipointSlot == i + 1)
                fraction += kMultipointStickyBonus;
            if (fraction > bestFraction) {
                bestFraction = fraction;
                best = candidate;
                bestSlot = i + 1;
            }
        }

        // Record the decision for the next tick. All five slots regenerate every tick around the
        // CURRENT geometry, so stickiness holds the CHOICE steady while the angles themselves
        // keep tracking moving enemies.
        stickyMultipointEntity = target.entity;
        stickyMultipointHitgroup = target.hitgroup;
        stickyMultipointSlot = bestSlot;
        return best;
    }

    // Estimated damage a shot to the aimed point would do: base weapon damage with distance falloff
    // (base * rangeModifier^(distance/500)) then velocity's scale_damage (headshot / stomach 1.25 / leg
    // 0.75 hitgroup scaling and the armor model). The server damage-scale convars are assumed 1.0 (their
    // defaults; a local nosecure server). Returns a large sentinel if the weapon params can't be read, so
    // a resolve failure never blocks the shot on a min-damage gate.
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

    // velocity's rage target visibility gate. With neither option enabled every FOV target is
    // accepted (the pre-gate behavior). WallCheck alone requires a clear world trace to the aimed
    // point - hitting the target itself counts as visible. Autowall relaxes that to walls: the shot
    // passes when Autowall::penetratedDamage (the game's own per-layer loss math) leaves at least
    // MinDamage after armor/hitgroup scaling; 0 MinDamage accepts any surviving damage. A ray
    // blocked by another PLAYER never passes, and unreadable weapon inputs fail closed so a missing
    // estimate can never manufacture a blind wallbang.
    [[nodiscard]] bool passesVisibility(auto&& localPawn, const cs2::Vector& eye, const typename AimTarget<HookContext>::Target& target) const noexcept
    {
        const bool wallCheck = GET_CONFIG_VAR(aimbot_vars::WallCheck);
        const bool autowall = GET_CONFIG_VAR(aimbot_vars::Autowall);
        if (!wallCheck && !autowall)
            return true;

        const auto fwd = Tracing::traceLine(eye, target.aimPoint, localPawn.baseEntity());
        if (!fwd.didHit || fwd.hitEntity == target.entity)
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

    // velocity-cs2's shared::penetration::scale_damage with the server damage-scale convars taken as 1.0.
    static void scaleDamage(float& damage, int hitgroup, int armor, bool hasHelmet, float armorRatio, float headshotMultiplier) noexcept
    {
        switch (hitgroup) {
        case 1: damage *= headshotMultiplier; break; // head
        case 3: damage *= 1.25f; break;              // stomach
        case 6: case 7: damage *= 0.75f; break;      // legs
        default: break;                              // chest / arms: x1
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

    // floor() clamped at 0 (no libm; truncation is floor for non-negative values, and a fully
    // armor-absorbed shot can go slightly negative).
    [[nodiscard]] static float floorNonNegative(float value) noexcept
    {
        if (value < 0.0f)
            return 0.0f;
        return static_cast<float>(static_cast<int>(value));
    }

    // Reads the target's armor value and helmet flag (velocity's prepare_target). Leaves the defaults
    // (0 / false) if a field or the item-services pointer doesn't resolve - which only makes the damage
    // estimate higher, i.e. the min-damage gate more permissive, never falsely blocking a lethal shot.
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

    // Which hitboxes the silent aimbot may target, from its own config. Passed to the shared
    // AimTarget selector (the legit aimbot builds the same struct from its own config vars). BodyAim
    // (velocity's force b-aim) overrides the toggles to target the chest, falling back to the stomach -
    // the reliable center-mass point for an auto-shoot while moving.
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

    // velocity-cs2's autostop. Reads the local pawn's m_vecVelocity, projects it onto the view-frame
    // forward/right axes and presses the counter-strafe buttons for whatever component is still fast:
    // moving forward -> press back, strafing right -> press left, etc. With the real key ALSO held the
    // two cancel and friction decelerates the player in a few ticks - a counter-strafe - which drops
    // GetInaccuracy to its standing-still value, where findSpreadCorrection is exact.
    //
    // Presses go through pressButtonsBothBanks: bank1 (held) + bank2 (changed this tick), raw words AND
    // buttons_pb - the same proven path the triggerbot's shot takes, so the server agrees with what the
    // client predicts. Bits only live in THIS command (rebuilt from real key state every tick), so not
    // pressing next tick releases everything; no stuck keys are possible.
    void autoStop(auto&& pawn, float viewYaw, const UserCmd& userCmd) const noexcept
    {
        const auto velocityOffset = hookContext.schemaSystem().getFieldOffset("C_BaseEntity", "m_vecVelocity");
        if (!velocityOffset.has_value() || *velocityOffset <= 0)
            return;

        const auto* const entityBytes = reinterpret_cast<const std::byte*>(static_cast<cs2::C_BaseEntity*>(pawn.baseEntity()));
        cs2::Vector velocity{};
        std::memcpy(&velocity, entityBytes + *velocityOffset, sizeof(velocity));

        // Source convention: yaw 0 faces +X, +Y is LEFT, so right = (sin(yaw), -cos(yaw)).
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
            (void)userCmd.pressButtonsBothBanks(counterButtons); // false = unsafe tick (map transition); autostop retried next tick
    }

    // Rage hitchance Monte-Carlo: 256 samples (matching the triggerbot / SpreadPredictor tests). The
    // sphere radius is a body-sized target around the aimed point - a bit generous so a real on-target
    // auto-shot is not rejected.
    static constexpr int kHitchanceSamples = 256;
    static constexpr float kHitchanceRadius = 12.0f;

    // Autostop: below this speed (u/s) the player counts as stopped - no counter-strafe pressed, so
    // normal movement resumes immediately after firing stops. Above ~1/3 of run speed is enough to
    // matter for inaccuracy while barely interfering with movement feel.
    static constexpr float kAutoStopSpeedThreshold = 60.0f;

    // Returned by estimatedDamage when the weapon params can't be read: large enough to pass any
    // min-damage threshold, so an unreadable weapon never blocks the shot on the damage gate.
    static constexpr float kUnknownDamage = 1000.0f;

    // Multipoint: fewer samples per candidate (4 ring points + centre) to keep the per-frame raycast cost
    // bounded, and the ring radius on the hitbox for head vs body.
    static constexpr int kMultipointSamples = 64;
    static constexpr float kMultipointHeadRadius = 3.5f;
    static constexpr float kMultipointBodyRadius = 10.0f;

    // Force-shot decision made during onCreateMove, consumed (and cleared) by this tick's
    // onWriteMoveCrc. Static for the same reason the triggerbot's armed/fireAtTime are: hookContext
    // builds a fresh feature instance per hook call, so cross-hook state must outlive it.
    inline static bool forceShotThisTick{false};
    // The shot staged at CreateMove, consumed at WriteMoveCrc (see onWriteMoveCrc for why the
    // write moved hooks). Statics because feature objects are rebuilt per call and the decision
    // spans two hooks.
    inline static bool shotStagedThisTick{false};
    // Target stickiness: the held target's full entity handle (index+serial) - 0 = none held.
    inline static std::uint32_t lastTargetHandleValue{0};
    inline static float stagedAimPitch{0.0f};

    // Multipoint stickiness state (see refineMultipoint): which entity/hitbox/slot won last tick.
    inline static const void* stickyMultipointEntity{nullptr};
    inline static int stickyMultipointHitgroup{-1};
    inline static int stickyMultipointSlot{0};
    inline static float stagedAimYaw{0.0f};
    inline static float stagedPunchPitch{0.0f};
    inline static float stagedPunchYaw{0.0f};
    inline static float stagedBacktrackSimTime{0.0f};
    inline static bool stagedSpreadCompensation{false};
    // Seed-mode fallback inputs: where the staged shot's eye and aimed hitbox point are, so the
    // WriteMoveCrc-side luck check can test the deflected ray against the real target geometry.
    inline static cs2::Vector stagedEye{};
    inline static cs2::Vector stagedAimPoint{};
    // The CreateMove-time spread correction (see the staging comment): consumed by the writer
    // for live shots and by the FVA chain arm. Validity flag matters - a failed solve must
    // reach the gate as "lands = false", not as "no correction requested".
    using SolverAngles = typename SpreadSolver<HookContext>::Angles;
    inline static bool stagedCorrectionValid{false};
    inline static SolverAngles stagedCorrectionAngles{};

    // Seed fallback: how close (units) the predicted deflected shot must land to the aimed
    // hitbox point for a held tick to fire anyway - head-sized.
    static constexpr float kSeedFallbackRadius = 6.0f;

    HookContext& hookContext;
};
