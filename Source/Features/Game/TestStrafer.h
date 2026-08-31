#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>

#include <CS2/Classes/CCSGOInput.h>
#include <CS2/Classes/CUserCmd.h>
#include <Features/Game/Bunnyhop.h>
#include <Features/Game/BunnyhopConfigVariables.h>
#include <GameClient/ConVars/CvarSystem.h>
#include <GameClient/Entities/BaseEntity.h>
#include <GameClient/Entities/PlayerPawn.h>
#include <GameClient/GlobalVars.h>
#include <GameClient/SubtickMoves.h>
#include <GameClient/UserCmd.h>
#include <HookContext/HookContextMacros.h>
#include <Utils/Optional.h>
#include <Utils/Trig.h>
#include <Utils/VerifyConsole.h>

// The quantized autostrafer - a faithful port of velocity-cs2's "test" strafer, with the classic
// analog strafe as the in-tick fallback.
//
// The quantized path: a command may carry up to 32 subtick moves, each accepting a pure
// view-angle adjustment (yaw_delta) at an exact moment through the tick; the server
// air-accelerates against the view direction it had AT that moment. Split the remaining part of
// the tick into up to 16 sub-frames, run the game's own air-acceleration math forward from the
// current networked velocity, compute the ideal gain angle for the simulated speed at EVERY
// sub-frame, and emit one yaw_delta step per sub-frame - alternating sides between steps. The
// capture half runs at CreateMove; the steps are WRITTEN at slot 7 (WriteMoveCrc), the last hook
// before checksumming - on this Linux build slot 6 (BuildUserCmd) rebuilds the subtick timeline
// from the input queue after CreateMove, so steps appended there never reach the wire.
//
// The fallback: velocity-cs2 never leaves an airborne tick unstrafed - its classic analog
// airstrafe runs for every tick the quantized one did not handle. Skipped ticks here hand off to
// the analog strafe through the bunnyhop (one ideal-angle correction per tick, input-side).
//
// The thresholds are the reference's: below 1 u/s there is no meaningful direction, below 15 u/s
// the best move is straight along the wish direction, |velocity-to-target| > 2 degrees means the
// player is deliberately steering and the strafer follows instead of optimizing, and steps stop
// where the game's existing subticks already end (never stacking onto them).
template <typename HookContext>
class TestStrafer {
public:
    explicit TestStrafer(HookContext& hookContext) noexcept
        : hookContext{hookContext}
    {
    }

    // CAPTURE PHASE - CreateMove, after the original filled the command in. The command's
    // subtick timeline was already cleared for this tick by the desubtick step in the hook
    // (before the bunnyhop wrote its taps).
    void onCreateMove(cs2::CUserCmd* cmd) noexcept
    {
        handledThisTick = false;

        const UserCmd userCmd{cmd};
        if (!userCmd)
            return;

        if (!GET_CONFIG_VAR(TestStraferEnabled))
            return;

        // Dead players do not strafe, and their flags mean nothing once they are a ragdoll.
        auto&& localPawn = hookContext.localPlayerController().pawn().template as<PlayerPawn>();
        if (!localPawn)
            return;
        const auto health = localPawn.health();
        if (!health.hasValue() || health.value() <= 0)
            return;

        // Only while walking. On a ladder or in noclip the simulation below models nothing real.
        const auto walking = isWalking(localPawn);
        if (!walking.hasValue() || !walking.value())
            return;

        // Ground movement does not need help, and simulating air acceleration while grounded is
        // wrong - it is also the landing edge of a hop, where the per-hop speed measurement logs.
        const auto onGround = isOnGround(localPawn);
        if (!onGround.hasValue())
            return;
        trackHopSpeed(onGround.value());

        if (onGround.value())
            return;

        const SkipReason reason = capturePath(userCmd);
        if (reason == SkipReason::none)
            captureValid = true;

        // One-per-second in-game state line while airborne (the [strafe] verification harness).
        VerifyConsole::write(1.0f, "[strafe]", "r=%s spd=%d q=%d inj=%d",
            skipReasonText(reason), static_cast<int>(diagSpeed), diagQuantized, lastInjectedCount);

        if (reason == SkipReason::none)
            return;

        // The reference's hand-off: a skipped tick still has movement input worth steering, so
        // the classic analog strafe takes it instead of the tick going to waste. Only the skips
        // that leave an airborne player with something to steer are handed over.
        switch (reason) {
        case SkipReason::sprint:
        case SkipReason::quantizeOff:
        case SkipReason::lowSpeed:
            static_cast<void>(hookContext.template make<Bunnyhop>().engageClassicStrafeAngle(
                userCmd, classicAngle(userCmd, localPawn), strafeSide));
            break;
        default:
            break;
        }
    }

    // WRITE PHASE - slot 7 (WriteMoveCrc), the last hook before the command is checksummed and
    // sent. Simulates the rest of the tick from the captured inputs and emits one yaw_delta step
    // per sub-frame.
    void onWriteMoveCrc(cs2::CUserCmd* cmd) noexcept
    {
        if (!captureValid)
            return;
        captureValid = false;

        const UserCmd userCmd{cmd};
        std::byte* const base = userCmd.baseMessage();
        if (!base)
            return;

        // Steps start after everything already scheduled this tick (the bunnyhop's landing taps
        // were written earlier in this same hook, plus whatever slot 6 rebuilt from the queue).
        const float startWhen = SubtickMoves<HookContext>::maxWhen(base);
        diagStartWhen = startWhen;
        if (startWhen >= 0.99f) {
            lastInjectedCount = 0;
            return;
        }

        const float subFrame = capturedTickInterval / static_cast<float>(kMaxSubticks);
        const float whenStep = (1.0f - startWhen) / static_cast<float>(kMaxSubticks + 1);

        float accumulatedYaw = capturedYaw;
        float simVx = capturedVx;
        float simVy = capturedVy;
        int injected = 0;

        for (int i = 1; i <= kMaxSubticks; ++i) {
            // Sides alternate between steps, and the parity carries ACROSS ticks, so the server
            // never sees two same-side accelerations back to back at a step boundary.
            const bool entrySide = ((substepCounter + i) % 2) == 0;

            // THE SWEEP IS ANCHORED TO THE VIEW, not to the velocity (the reference anchors to
            // the velocity and relies on a pull branch that leaves the direction untracked when
            // the server-side oscillation is not rendered for the player). With the wish
            // alternating view +/- theta: the perpendicular components cancel over pairs of
            // steps, the forward components add, and the NET acceleration of every tick points
            // at the look direction - so the velocity curves toward wherever the player aims
            // while the sweep keeps each step's dot just under the air_max_wishspeed cap, which
            // is where the gain comes from.
            const float simSpeed = trig::squareRoot(simVx * simVx + simVy * simVy);
            const float theta = idealAngle(simSpeed, subFrame, capturedMaxSpeed,
                capturedAirAccelerate, capturedAirMaxWishSpeed);
            const float wishdirYaw = trig::normalizeDegrees(capturedTargetYaw + (entrySide ? theta : -theta));

            // Translate the ideal wish yaw back into a view yaw the command can carry.
            const float targetViewYaw = trig::normalizeDegrees(wishdirYaw - capturedBaseYawOffset);
            const float yawDelta = trig::normalizeDegrees(targetViewYaw - accumulatedYaw);

            // Sub-degree corrections are below what the quantizer acts on - and stopping here
            // rather than skipping matches the reference: once the simulation is aligned, later
            // slices would only re-derive the same zero correction.
            if (trig::absolute(yawDelta) <= 0.01f)
                break;

            const float when = startWhen + static_cast<float>(i) * whenStep;

            auto* const step = hookContext.template make<SubtickMoves>().add(base, when);
            if (!step)
                break;
            // Field-for-field what the reference's apply_yaw_subtick writes: a pure view
            // rotation with every other component explicitly zeroed, so the step is
            // indistinguishable from one the game's own quantized input path produced.
            SubtickMoves<HookContext>::setButton(step, 0, false);
            SubtickMoves<HookContext>::setAnalogDeltas(step, 0.0f, 0.0f);
            SubtickMoves<HookContext>::setYawDelta(step, yawDelta);
            SubtickMoves<HookContext>::setPitchDelta(step, 0.0f);

            accumulatedYaw = targetViewYaw;
            airAccelSim(simVx, simVy, wishdirYaw, subFrame, capturedFriction,
                capturedMaxSpeed, capturedAirAccelerate, capturedAirMaxWishSpeed);
            ++injected;
        }

        lastInjectedCount = injected;
        if (injected > 0) {
            handledThisTick = true;
            ++substepCounter;
        }
    }

private:
    // Why the strafer stayed off this tick, in capturePath order. (Timeline-full and no-steps
    // conditions are detected in the slot-7 write phase and show up as inj=0 in the [strafe]
    // line instead of a skip reason.)
    enum class SkipReason {
        none,
        sprint,
        quantizeOff,
        noVelocity,
        noViewYaw,
        noMovementKeys,
        lowSpeed,
        noBaseMessage,
        noMovementConVars
    };

    [[nodiscard]] static const char* skipReasonText(SkipReason reason) noexcept
    {
        switch (reason) {
        case SkipReason::none: return "captured";
        case SkipReason::sprint: return "sprint-walking";
        case SkipReason::quantizeOff: return "quantized movement input off";
        case SkipReason::noVelocity: return "no local velocity";
        case SkipReason::noViewYaw: return "no command view yaw";
        case SkipReason::noMovementKeys: return "no movement keys held";
        case SkipReason::lowSpeed: return "speed below 5 u/s";
        case SkipReason::noBaseMessage: return "command has no base message";
        case SkipReason::noMovementConVars: return "air-accelerate cvars unreadable";
        }
        return "unknown";
    }

    // View-relative movement components: x forward, y left, matching the rest of this project.
    struct StrafeMove {
        float forward{};
        float left{};
    };

    // The capture half of the per-tick driver: track the player's own movement keys, validate
    // the tick and record every input the slot-7 write phase simulates from.
    [[nodiscard]] SkipReason capturePath(const UserCmd& userCmd) noexcept
    {
        // velocity-cs2 gates on this cvar: when the server runs with quantized movement input
        // off, yaw_delta steps are ignored outright. The cvar exists in this binary
        // (client-registered); a failed lookup must not kill the feature - fail open instead.
        const auto quantized = hookContext.template make<CvarSystem>().readBoolConVar("sv_quantize_movement_input");
        diagQuantized = quantized.has_value() ? (quantized.value() ? 1 : 0) : -1;
        if (quantized.has_value() && !quantized.value())
            return SkipReason::quantizeOff;

        if (userCmd.isButtonDown(kSprintButton))
            return SkipReason::sprint;

        trackMovementKeys(userCmd);

        const auto velocity = localVelocity();
        if (!velocity.hasValue())
            return SkipReason::noVelocity;

        const auto commandYaw = userCmd.viewYaw();
        if (!commandYaw.hasValue())
            return SkipReason::noViewYaw;

        // No movement keys means no wish direction - nothing to strafe with or around.
        const StrafeMove playerMove = movementFromButtons();
        if (playerMove.forward == 0.0f && playerMove.left == 0.0f)
            return SkipReason::noMovementKeys;

        const float speed2d = trig::squareRoot(velocity.value().x * velocity.value().x + velocity.value().y * velocity.value().y);
        diagSpeed = speed2d;
        if (speed2d < kMinStrafeSpeed)
            return SkipReason::lowSpeed;

        std::byte* const base = userCmd.baseMessage();
        if (!base)
            return SkipReason::noBaseMessage;

        const auto airAccelerate = conVarFloat<cs2::sv_airaccelerate>();
        const auto maxSpeed = conVarFloat<cs2::sv_maxspeed>();
        const auto airMaxWishSpeed = conVarFloat<cs2::sv_air_max_wishspeed>();
        if (!airAccelerate.hasValue() || !maxSpeed.hasValue() || !airMaxWishSpeed.hasValue())
            return SkipReason::noMovementConVars;
        if (airAccelerate.value() <= 0.0f || maxSpeed.value() <= 0.0f)
            return SkipReason::noMovementConVars;

        // Ground friction of the surface below: enters the simulated acceleration exactly as the
        // game applies it. If it cannot be read, 1.0 (default friction) degrades gracefully.
        const float friction = surfaceFriction().valueOr(1.0f);

        const auto tickInterval = hookContext.globalVars().tickInterval();
        if (!tickInterval.hasValue() || tickInterval.value() <= 0.0f)
            return SkipReason::noMovementConVars;

        // The wish direction the player's held keys describe, relative to straight ahead: this
        // is what "follow the player's intent" means in yaw terms.
        const float baseYawOffset = trig::arcTangent2(-playerMove.left, playerMove.forward) * trig::kRadiansToDegrees;

        // Everything the slot-7 write phase needs, captured while the pawn state is current.
        capturedVx = velocity.value().x;
        capturedVy = velocity.value().y;
        capturedYaw = commandYaw.value();
        capturedTargetYaw = trig::normalizeDegrees(commandYaw.value() + baseYawOffset);
        capturedBaseYawOffset = baseYawOffset;
        capturedAirAccelerate = airAccelerate.value();
        capturedMaxSpeed = maxSpeed.value();
        capturedAirMaxWishSpeed = airMaxWishSpeed.value();
        capturedFriction = friction;
        capturedTickInterval = tickInterval.value();
        return SkipReason::none;
    }

    // The classic fallback's gain angle (one tick of air acceleration) and side alternation -
    // the same ideal-angle math at tick dt, for the ticks the quantized path skips.
    [[nodiscard]] float classicAngle(const UserCmd& userCmd, auto&& localPawn) noexcept
    {
        const auto velocity = localVelocity();
        const auto viewYaw = userCmd.viewYaw();
        const auto airAccelerate = conVarFloat<cs2::sv_airaccelerate>();
        const auto maxSpeed = conVarFloat<cs2::sv_maxspeed>();
        const auto airMaxWishSpeed = conVarFloat<cs2::sv_air_max_wishspeed>();
        const auto tickInterval = hookContext.globalVars().tickInterval();
        if (!velocity.hasValue() || !viewYaw.hasValue() || !airAccelerate.hasValue()
            || !maxSpeed.hasValue() || !airMaxWishSpeed.hasValue() || !tickInterval.hasValue())
            return 0.0f;

        const float speed = trig::squareRoot(velocity.value().x * velocity.value().x + velocity.value().y * velocity.value().y);
        const float friction = surfaceFriction().valueOr(1.0f);
        const float halfAccel = 0.5f * airAccelerate.value() * maxSpeed.value() * tickInterval.value() * friction;
        const float optimalFloor = std::max(halfAccel, airMaxWishSpeed.value() - halfAccel);
        const float angle = std::clamp(trig::arcTangent2(optimalFloor, std::max(speed, 1.0f)) * trig::kRadiansToDegrees, 0.0f, 45.0f);

        strafeSide = !strafeSide;
        return angle;
    }

    // The gain angle for the CURRENT simulated speed: how far off the velocity the wish direction
    // must sit for air acceleration to add speed without being clamped away. Below 1 u/s the
    // direction is meaningless; the reference answers 15 degrees there and so does this.
    [[nodiscard]] static float idealAngle(float speed, float dt, float wishspeed, float airAccelerate, float airMaxWishSpeed) noexcept
    {
        if (speed < 1.0f)
            return 15.0f;

        const float accelSpeed = wishspeed * airAccelerate * dt;
        float cosTheta{};
        if (accelSpeed >= airMaxWishSpeed)
            cosTheta = airMaxWishSpeed / (2.0f * speed);
        else
            cosTheta = (airMaxWishSpeed - accelSpeed) / speed;

        cosTheta = std::clamp(cosTheta, -1.0f, 1.0f);
        const float angleDegrees = trig::arcCosine(cosTheta) * trig::kRadiansToDegrees;
        return angleDegrees < 1.0f ? 1.0f : angleDegrees;
    }

    // One slice of Source's AirAccelerate, run forward on the SIMULATED velocity so the next
    // slice's ideal angle is computed from where this one lands, not from stale input.
    static void airAccelSim(float& velX, float& velY, float wishdirYaw, float frameTime, float friction,
        float wishspeed, float airAccelerate, float airMaxWishSpeed) noexcept
    {
        const float yawRadians = wishdirYaw * trig::kDegreesToRadians;
        const float wishDirX = trig::cosine(yawRadians);
        const float wishDirY = trig::sine(yawRadians);

        const float capped = wishspeed < airMaxWishSpeed ? wishspeed : airMaxWishSpeed;
        const float dot = velX * wishDirX + velY * wishDirY;
        const float addSpeed = capped - dot;
        if (addSpeed <= 0.0f)
            return;

        const float accelSpeed = wishspeed * airAccelerate * friction * frameTime;
        const float step = accelSpeed < addSpeed ? accelSpeed : addSpeed;

        velX += wishDirX * step;
        velY += wishDirY * step;
    }

    // The reference's movement-key edge tracker: a key counts as pressed from the tick it goes
    // down (or when its OPPOSITE comes up while it is held), and stops counting the moment it is
    // released. The opposite-release rule is what makes transitions like W+D -> A feel instant.
    void trackMovementKeys(const UserCmd& userCmd) noexcept
    {
        using Buttons = cs2::CCSGOInput::Buttons;

        trackKey(userCmd, Buttons::kMoveLeft);
        trackKey(userCmd, Buttons::kMoveRight);
        trackKey(userCmd, Buttons::kForward);
        trackKey(userCmd, Buttons::kBack);

        lastButtons = userCmd.buttonState1();
    }

    void trackKey(const UserCmd& userCmd, std::uint64_t button) noexcept
    {
        using Buttons = cs2::CCSGOInput::Buttons;

        const bool held = userCmd.isButtonDown(button);
        if (held && (!(lastButtons & button)
                || (button == Buttons::kMoveLeft && !(lastPressed & Buttons::kMoveRight))
                || (button == Buttons::kMoveRight && !(lastPressed & Buttons::kMoveLeft))
                || (button == Buttons::kForward && !(lastPressed & Buttons::kBack))
                || (button == Buttons::kBack && !(lastPressed & Buttons::kForward)))) {
            if (button == Buttons::kMoveLeft)
                lastPressed &= ~Buttons::kMoveRight;
            else if (button == Buttons::kMoveRight)
                lastPressed &= ~Buttons::kMoveLeft;
            else if (button == Buttons::kForward)
                lastPressed &= ~Buttons::kBack;
            else if (button == Buttons::kBack)
                lastPressed &= ~Buttons::kForward;

            lastPressed |= button;
        } else if (!held) {
            lastPressed &= ~button;
        }
    }

    // What the tracked keys describe, in view-relative components.
    [[nodiscard]] StrafeMove movementFromButtons() const noexcept
    {
        using Buttons = cs2::CCSGOInput::Buttons;

        float forwardMove{0.0f};
        float leftMove{0.0f};

        if (lastPressed & Buttons::kForward)
            forwardMove = 1.0f;
        else if (lastPressed & Buttons::kBack)
            forwardMove = -1.0f;

        if (lastPressed & Buttons::kMoveLeft)
            leftMove = -1.0f;
        else if (lastPressed & Buttons::kMoveRight)
            leftMove = 1.0f;

        return {forwardMove, leftMove};
    }

    // Per-hop speed measurement (the [strafehop] line): the peak horizontal speed of each air
    // phase and its delta against the previous hop. This is the objective "is it working"
    // answer - the quantized strafe's gain shows up here as rising peaks, regardless of what
    // the player feels.
    void trackHopSpeed(bool onGround) noexcept
    {
        if (onGround) {
            if (wasAirborne && hopPeak > 0.0f) {
                VerifyConsole::write(0.5f, "[strafehop]", "peak=%.1f delta=%+.1f", hopPeak, hopPeak - lastHopPeak);
                lastHopPeak = hopPeak;
            }
            hopPeak = 0.0f;
            wasAirborne = false;
            return;
        }

        wasAirborne = true;
        const auto velocity = localVelocity();
        if (velocity.hasValue()) {
            const float speed = trig::squareRoot(velocity.value().x * velocity.value().x + velocity.value().y * velocity.value().y);
            if (speed > hopPeak)
                hopPeak = speed;
        }
    }

    [[nodiscard]] Optional<cs2::Vector> localVelocity() const noexcept
    {
        auto&& localPawn = hookContext.localPlayerController().pawn().template as<PlayerPawn>();
        if (!localPawn)
            return {};

        const auto offset = hookContext.schemaSystem().getFieldOffset("C_BaseEntity", "m_vecAbsVelocity");
        if (!offset.has_value() || *offset <= 0)
            return {};

        cs2::Vector velocity{};
        std::memcpy(&velocity, reinterpret_cast<const std::byte*>(static_cast<cs2::C_BaseEntity*>(localPawn.baseEntity())) + *offset, sizeof(velocity));
        return velocity;
    }

    // m_pMovementServices / m_flSurfaceFriction off the pawn's movement services - the
    // multiplier the game itself puts into air acceleration (see Bunnyhop for the trail).
    [[nodiscard]] Optional<float> surfaceFriction() const noexcept
    {
        auto&& localPawn = hookContext.localPlayerController().pawn().template as<PlayerPawn>();
        if (!localPawn)
            return {};

        const auto servicesOffset = hookContext.schemaSystem().getFieldOffset("C_BasePlayerPawn", "m_pMovementServices");
        const auto frictionOffset = hookContext.schemaSystem().getFieldOffset("CCSPlayer_MovementServices", "m_flSurfaceFriction");
        if (!servicesOffset.has_value() || *servicesOffset <= 0 || !frictionOffset.has_value() || *frictionOffset <= 0)
            return {};

        void* services = nullptr;
        std::memcpy(&services, reinterpret_cast<const std::byte*>(static_cast<cs2::C_BaseEntity*>(localPawn.baseEntity())) + *servicesOffset, sizeof(services));
        if (!services)
            return {};

        float friction{};
        std::memcpy(&friction, reinterpret_cast<const std::byte*>(services) + *frictionOffset, sizeof(friction));
        return friction;
    }

    template <typename ConVarType>
    [[nodiscard]] Optional<float> conVarFloat() const noexcept
    {
        const auto value = hookContext.template make<CvarSystem>().template getConVarValue<ConVarType>();
        if (!value.has_value())
            return {};
        return *value;
    }

    // MOVETYPE_WALK, from the game's own MOVETYPE name table (see Bunnyhop for the full trail).
    static constexpr std::uint8_t kMoveTypeWalk = 2;

    [[nodiscard]] Optional<bool> isWalking(auto&& localPawn) const noexcept
    {
        const auto offset = hookContext.schemaSystem().getFieldOffset("C_BaseEntity", "m_MoveType");
        if (!offset.has_value() || *offset <= 0)
            return {};

        std::uint8_t moveType{};
        std::memcpy(&moveType, reinterpret_cast<const std::byte*>(static_cast<cs2::C_BaseEntity*>(localPawn.baseEntity())) + *offset, sizeof(moveType));
        return moveType == kMoveTypeWalk;
    }

    // FL_ONGROUND, mask 0x1 out of the game's own flag-name table.
    static constexpr std::uint32_t kOnGroundFlag = 0x1;

    [[nodiscard]] Optional<bool> isOnGround(auto&& localPawn) const noexcept
    {
        const auto offset = hookContext.schemaSystem().getFieldOffset("C_BaseEntity", "m_fFlags");
        if (!offset.has_value() || *offset <= 0)
            return {};

        std::uint32_t flags{};
        std::memcpy(&flags, reinterpret_cast<const std::byte*>(static_cast<cs2::C_BaseEntity*>(localPawn.baseEntity())) + *offset, sizeof(flags));
        return (flags & kOnGroundFlag) != 0;
    }

    static constexpr int kMaxSubticks = 16;
    static constexpr float kMinStrafeSpeed = 5.0f;

    // IN_SPRINT (shift-walk), 1 << 16 in the game's own button mask table. Sprinting is a
    // deliberate slow-movement choice, so it opts the player out of acceleration steering.
    static constexpr std::uint64_t kSprintButton = 0x10000;

    // True from capture until the slot-7 write consumes it (one-shot per tick).
    inline static bool captureValid{false};

    // Captured simulation inputs (capture phase -> write phase within one tick).
    inline static float capturedVx{0.0f};
    inline static float capturedVy{0.0f};
    inline static float capturedYaw{0.0f};
    inline static float capturedTargetYaw{0.0f};
    inline static float capturedBaseYawOffset{0.0f};
    inline static float capturedAirAccelerate{0.0f};
    inline static float capturedMaxSpeed{0.0f};
    inline static float capturedAirMaxWishSpeed{0.0f};
    inline static float capturedFriction{1.0f};
    inline static float capturedTickInterval{0.0f};

    // The reference's handled_this_tick, set by the slot-7 write phase once steps went out.
    inline static bool handledThisTick{false};
    inline static std::uint64_t lastButtons{0};
    inline static std::uint64_t lastPressed{0};
    inline static int substepCounter{0};

    // [strafe] diagnostics.
    inline static int lastInjectedCount{0};
    inline static float diagSpeed{0.0f};
    inline static float diagStartWhen{0.0f};
    inline static int diagQuantized{-1};

    // [strafehop] per-hop peak speed measurement.
    inline static bool wasAirborne{false};
    inline static float hopPeak{0.0f};
    inline static float lastHopPeak{0.0f};

    // The alternation state for the classic fallback's aligned branch.
    inline static bool strafeSide{false};

    HookContext& hookContext;
};
