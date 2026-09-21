#pragma once

#include <cstdint>
#include <cstring>

#include <CS2/Classes/CUserCmd.h>
#include <CS2/Classes/Entities/C_CSPlayerPawn.h>
#include <CS2/Classes/Vector.h>
#include <CS2/Classes/CCSGOInput.h>
#include <Features/Game/BlockbotConfigVariables.h>
#include <GameClient/CSGOInputMovement.h>
#include <GameClient/Entities/BaseEntity.h>
#include <GameClient/Entities/PlayerPawn.h>
#include <GameClient/EntitySystem/EntitySystem.h>
#include <GameClient/KeyboardState.h>
#include <GameClient/UserCmd.h>
#include <HookContext/HookContextMacros.h>
#include <SDL/SdlFunctions.h>
#include <Utils/Trig.h>

// Stands in front of a player and mirrors their sideways movement, blocking them from advancing.
//
// Runs on the movement-input path (CCSGOInput::CreateMove), AFTER the original has built the
// command - so it edits a finished command rather than competing with the player's own input.
//
// Held-key activated: the toggle arms the feature, and it only steers while the key is physically
// down. That is deliberate for something that takes movement away from you - releasing the key
// always returns control immediately, with no state to get stuck in.
template <typename HookContext>
class Blockbot {
public:
    explicit Blockbot(HookContext& hookContext) noexcept
        : hookContext{hookContext}
    {
    }

    void onCreateMove(cs2::CUserCmd* cmd) const noexcept
    {
        if (!GET_CONFIG_VAR(BlockbotEnabled) || !KeyboardState::isKeyDown(sdl3::scancode::kE))
            return stop();

        const UserCmd userCmd{cmd};
        if (!userCmd)
            return stop();

        const auto viewYaw = userCmd.viewYaw();
        if (!viewYaw.hasValue())
            return stop();

        auto&& localPawn = hookContext.localPlayerController().pawn().template as<PlayerPawn>();
        if (!localPawn)
            return stop();

        const auto localOrigin = localPawn.absOrigin();
        if (!localOrigin.hasValue())
            return stop();

        const auto target = acquireTarget(localOrigin.value(), viewYaw.value());
        if (!target.found)
            return stop();

        // Steer from where WE will be, not where we are. This is the damping term: without it the
        // controller keeps commanding movement it has already earned, overshoots, reverses, and
        // sits there vibrating instead of closing on the target.
        const auto localPredicted = extrapolate(localPawn, localOrigin.value(), kLocalExtrapolationTicks, true);

        steer(userCmd, localPredicted, localOrigin.value(), target, viewYaw.value());
    }

    // Runs BEFORE CCSGOInput slot 6, the function that turns queued input into movement.
    //
    // Applies the move decided by the last onCreateMove, which is at most one tick old - the two
    // run in the same frame. That staleness is deliberate and cheap: the alternative is duplicating
    // the whole target/geometry pass here without a command to read the view yaw from, and one tick
    // is nothing next to the three ticks of extrapolation the controller already applies.
    //
    // Gated on the toggle and the key as well as on a pending move, so that switching the feature
    // off can never leave input being rewritten.
    void onBuildUserCmd(cs2::CCSGOInput* input, int slot) const noexcept
    {
        // Slot 6 itself returns immediately for anything but slot 0, so acting on another slot
        // would be writing input the game is about to ignore.
        if (slot != 0)
            return;

        if (!hasPendingMove || !GET_CONFIG_VAR(BlockbotEnabled) || !KeyboardState::isKeyDown(sdl3::scancode::kE))
            return;

        const CSGOInputMovement movement{input};
        if (!movement)
            return;

        movement.setMove(pendingForward, pendingLeft);
    }

    // Presses the movement keys that correspond to the pending move, on the command slot 7 is about
    // to copy into `buttons_pb` and checksum into `move_crc`.
    //
    // This is the half that was missing. Analog movement alone was measured not to move the player
    // even with the game building the entire command from our injected input, and the button words
    // are the one thing a real keypress also sets.
    //
    // They live on the COMMAND (+96/+104/+112). An earlier attempt pressed them in CCSGOInput's own
    // accumulator instead, which cannot work - nothing copies that accumulator into a command - and
    // was in fact writing into an unrelated global, because the pointer it resolved was a manager
    // slot 6 passes to sub_15DA950, not a command at all. Slot 7 is handed the real one.
    void onWriteMoveCrc(cs2::CUserCmd* cmd) const noexcept
    {
        if (!cmd || !hasPendingMove || !GET_CONFIG_VAR(BlockbotEnabled) || !KeyboardState::isKeyDown(sdl3::scancode::kE))
            return;

        std::uint64_t buttons{};

        if (pendingForward > 0.0f)
            buttons |= cs2::CCSGOInput::Buttons::kForward;
        else if (pendingForward < 0.0f)
            buttons |= cs2::CCSGOInput::Buttons::kBack;

        if (pendingLeft > 0.0f)
            buttons |= cs2::CCSGOInput::Buttons::kMoveLeft;
        else if (pendingLeft < 0.0f)
            buttons |= cs2::CCSGOInput::Buttons::kMoveRight;

        UserCmd{cmd}.pressButtons(buttons);
    }

    // Forgets the target and drops any pending move. Steering leaves no held state behind - the
    // command is rebuilt from scratch by the game every tick and we simply stop feeding it - so
    // there is nothing to release here.
    void onUnload() const noexcept
    {
        stop();
    }

private:
    void stop() const noexcept
    {
        // Must clear the pending move too, not just the target: the slot 6 hook applies whatever is
        // pending, so leaving a stale one set would keep rewriting the player's own input after the
        // feature had stopped steering.
        hasPendingMove = false;

        // Forget the target, so re-pressing the key picks one for the situation you are in now
        // rather than resuming an old one.
        stickyTargetIndex = kNoTarget;
    }

    // The move handed from onCreateMove to the slot 6 hook. Constant-initialised and trivially
    // destructible, so no __cxa_guard under -nostdlib.
    inline static bool hasPendingMove{false};
    inline static float pendingForward{0.0f};
    inline static float pendingLeft{0.0f};

    static constexpr int kNoTarget = -1;

    // How far in front of us to centre the search, and how far from that centre a player can be and
    // still be picked up. Searching around a point ahead of the player rather than around the
    // player biases target choice towards whoever we are facing.
    static constexpr float kSearchForwardOffset = 32.0f;
    static constexpr float kSearchRadius = 256.0f;

    // Hysteresis: a target already held is kept until it goes further than this, which is larger
    // than the radius needed to acquire one. Without the gap, a target hovering exactly at the
    // acquisition distance would be dropped and re-acquired every other tick.
    static constexpr float kRetentionRadius = 384.0f;

    static constexpr float kTickInterval = 0.015625f;

    // Led by different amounts on purpose, matching the reference: our own position is the one the
    // controller is steering, so it looks further ahead than the target's.
    static constexpr float kLocalExtrapolationTicks = 4.0f;
    static constexpr float kTargetExtrapolationTicks = 2.0f;

    // Below this the direction of travel is noise and friction would do nothing useful anyway.
    static constexpr float kMinimumFrictionSpeed = 0.1f;

    // Dead-band in DEGREES of bearing error. Small on purpose - the reference reacts at roughly
    // half a degree - because anything larger is felt directly as a hole around the crosshair that
    // the target can sit in. It only exists to stop mouse jitter flipping the strafe every tick.
    static constexpr float kDeadBandDegrees = 0.5f;

    // Full deflection. The game's own square-walk debug feature writes 1.0f into forwardmove, so
    // that is the scale these fields are in - not the 450 speed units CS:GO used.
    static constexpr float kFullDeflection = 1.0f;

    // Standing on someone's head: close enough horizontally, and above their eye line. Within this
    // state the bot also drives forwards to stay on top of them rather than sliding off.
    static constexpr float kOnHeadRadius = 45.0f;

    // Used only if the view-offset field cannot be resolved from the schema. A standing CS2 player
    // is about this tall to the eyes, so the on-head test degrades to slightly conservative rather
    // than wrong.
    static constexpr float kFallbackEyeHeight = 64.0f;

    struct Target {
        bool found{false};
        cs2::Vector position{};
        float eyeZ{};
        int entityIndex{kNoTarget};
    };

    // Keeps the current target across ticks, re-acquiring only when it is gone, dead, or too far.
    //
    // Re-picking the closest target every tick looks equivalent but is not: with two bots at nearly
    // equal distance the choice flips back and forth and the controller is handed a different
    // setpoint each tick, so it chases neither.
    [[nodiscard]] Target acquireTarget(const cs2::Vector& localOrigin, float viewYaw) const noexcept
    {
        if (stickyTargetIndex != kNoTarget) {
            const auto held = findTargetByIndex(stickyTargetIndex, localOrigin);
            if (held.found)
                return held;
        }

        const auto acquired = findClosestTarget(localOrigin, viewYaw);
        stickyTargetIndex = acquired.entityIndex;
        return acquired;
    }

    [[nodiscard]] Target findTargetByIndex(int entityIndex, const cs2::Vector& localOrigin) const noexcept
    {
        Target result{};

        forEachCandidate([&](auto&& pawn, const cs2::Vector& origin) {
            if (result.found || pawn.baseEntity().handle().index().value != entityIndex)
                return;

            const float deltaX = origin.x - localOrigin.x;
            const float deltaY = origin.y - localOrigin.y;
            if (deltaX * deltaX + deltaY * deltaY > kRetentionRadius * kRetentionRadius)
                return;

            result = makeTarget(pawn, origin, entityIndex);
        });

        return result;
    }

    [[nodiscard]] Target findClosestTarget(const cs2::Vector& localOrigin, float viewYaw) const noexcept
    {
        const float yawRadians = viewYaw * trig::kDegreesToRadians;
        const float searchX = localOrigin.x + trig::cosine(yawRadians) * kSearchForwardOffset;
        const float searchY = localOrigin.y + trig::sine(yawRadians) * kSearchForwardOffset;

        Target best{};
        // Compared squared throughout - identical ordering, no square root per candidate.
        float bestDistanceSquared = kSearchRadius * kSearchRadius;

        forEachCandidate([&](auto&& pawn, const cs2::Vector& origin) {
            const float deltaX = origin.x - searchX;
            const float deltaY = origin.y - searchY;
            const float distanceSquared = deltaX * deltaX + deltaY * deltaY;

            if (distanceSquared < bestDistanceSquared) {
                bestDistanceSquared = distanceSquared;
                best = makeTarget(pawn, origin, pawn.baseEntity().handle().index().value);
            }
        });

        return best;
    }

    // Every living, non-local player pawn that has a usable position.
    void forEachCandidate(auto&& handler) const noexcept
    {
        hookContext.template make<EntitySystem>().forEachNetworkableEntityIdentity([&](const auto& entityIdentity) {
            auto&& baseEntity = hookContext.template make<BaseEntity>(static_cast<cs2::C_BaseEntity*>(entityIdentity.entity));
            if (!baseEntity.classify().template is<cs2::C_CSPlayerPawn>())
                return;

            auto&& pawn = baseEntity.template as<PlayerPawn>();
            if (!pawn || pawn.isControlledByLocalPlayer())
                return;

            const auto health = pawn.health();
            if (!health.hasValue() || health.value() <= 0)
                return;

            const auto origin = pawn.absOrigin();
            if (!origin.hasValue())
                return;

            handler(pawn, origin.value());
        });
    }

    [[nodiscard]] Target makeTarget(auto&& pawn, const cs2::Vector& origin, int entityIndex) const noexcept
    {
        Target target{};
        target.found = true;
        target.position = extrapolate(pawn, origin, kTargetExtrapolationTicks, false);
        target.eyeZ = origin.z + eyeHeight(pawn);
        target.entityIndex = entityIndex;
        return target;
    }

    [[nodiscard]] float eyeHeight(auto&& pawn) const noexcept
    {
        const auto offset = hookContext.schemaSystem().getFieldOffset("C_BasePlayerPawn", "m_vecViewOffset");
        if (!offset.has_value() || *offset <= 0)
            return kFallbackEyeHeight;

        cs2::Vector viewOffset{};
        std::memcpy(&viewOffset, reinterpret_cast<const std::byte*>(static_cast<cs2::C_BaseEntity*>(pawn.baseEntity())) + *offset, sizeof(viewOffset));
        return viewOffset.z > 0.0f ? viewOffset.z : kFallbackEyeHeight;
    }

    // Where an entity will be in `leadTicks`, from its own velocity.
    //
    // OUR OWN prediction has ground friction applied first, the target's does not. A player who is
    // no longer pressing anything is already decelerating, so leading on raw velocity overshoots
    // and the controller spends its time correcting an error it invented. The reference applies
    // friction to the local player for exactly this reason, and leads the two by different amounts.
    [[nodiscard]] cs2::Vector extrapolate(auto&& pawn, const cs2::Vector& origin, float leadTicks, bool applyFriction) const noexcept
    {
        const auto offset = hookContext.schemaSystem().getFieldOffset("C_BaseEntity", "m_vecAbsVelocity");
        if (!offset.has_value() || *offset <= 0)
            return origin;

        cs2::Vector velocity{};
        std::memcpy(&velocity, reinterpret_cast<const std::byte*>(static_cast<cs2::C_BaseEntity*>(pawn.baseEntity())) + *offset, sizeof(velocity));

        if (applyFriction)
            applyGroundFriction(pawn, velocity);

        const float lead = kTickInterval * leadTicks;
        return cs2::Vector{origin.x + velocity.x * lead, origin.y + velocity.y * lead, origin.z};
    }

    // One tick of CGameMovement::Friction. Leaves the velocity untouched if the cvars cannot be
    // read, which degrades to the old raw-velocity lead rather than to something wrong.
    void applyGroundFriction(auto&& pawn, cs2::Vector& velocity) const noexcept
    {
        const float speed = trig::squareRoot(velocity.x * velocity.x + velocity.y * velocity.y);
        if (speed < kMinimumFrictionSpeed)
            return;

        const auto friction = conVarFloat<cs2::sv_friction>();
        const auto stopSpeed = conVarFloat<cs2::sv_stopspeed>();
        if (!friction.hasValue() || !stopSpeed.hasValue())
            return;

        // sv_friction scaled by the surface the player is standing on, which is what the movement
        // code itself multiplies by.
        const float control = speed < stopSpeed.value() ? stopSpeed.value() : speed;
        const float drop = control * friction.value() * surfaceFriction(pawn) * kTickInterval;

        const float newSpeed = speed - drop > 0.0f ? speed - drop : 0.0f;
        const float scale = newSpeed / speed;
        velocity.x *= scale;
        velocity.y *= scale;
    }

    [[nodiscard]] float surfaceFriction(auto&& pawn) const noexcept
    {
        const auto offset = hookContext.schemaSystem().getFieldOffset("C_BaseEntity", "m_flFriction");
        if (!offset.has_value() || *offset <= 0)
            return 1.0f;

        float friction{};
        std::memcpy(&friction, reinterpret_cast<const std::byte*>(static_cast<cs2::C_BaseEntity*>(pawn.baseEntity())) + *offset, sizeof(friction));
        return friction > 0.0f ? friction : 1.0f;
    }

    template <typename ConVarType>
    [[nodiscard]] Optional<float> conVarFloat() const noexcept
    {
        const auto value = hookContext.template make<CvarSystem>().template getConVarValue<ConVarType>();
        if (!value.has_value())
            return {};
        return *value;
    }

    // Proportional control on the target's offset in view space.
    //
    // Inside the dead-band this writes NOTHING. Writing a zero would stomp the player's own input
    // every tick - the command already holds their input when we get it, so "not steering" has to
    // mean leaving it alone. The reference implementation applies its output the same way, and the
    // game itself does the same in reverse: slot 6 CLEARS the has-bit when a move value is exactly
    // zero rather than sending a zero.
    void steer(const UserCmd& userCmd, const cs2::Vector& localPredicted, const cs2::Vector& localOrigin, const Target& target, float viewYaw) const noexcept
    {
        const float deltaX = target.position.x - localPredicted.x;
        const float deltaY = target.position.y - localPredicted.y;

        // Steering on the ANGLE to the target, not on how far sideways it has drifted in world
        // units. This is the whole difference between a bot that holds someone and one with a hole
        // around the crosshair.
        //
        // The old measure was the lateral offset in units, with a 4-unit dead-band and full
        // deflection at 24. At any normal blocking distance those translate to roughly two degrees
        // before it reacted at all and THIRTEEN before it moved at full speed - so the target could
        // walk most of the way off the crosshair before anything happened. In angle the threshold
        // is the same however close or far the target is, which is what the reference does.
        const float bearing = trig::yawTo(deltaX, deltaY);
        const float angleError = trig::normalizeDegrees(bearing - viewYaw);

        // Bang-bang, not proportional. Air and ground acceleration both take time to reach full
        // speed, so easing in with the error just means arriving late; the reference commits to a
        // full press the moment the error clears the dead-band, and only the dead-band keeps it
        // from chattering on mouse jitter.
        float desiredLeft = 0.0f;
        if (angleError > kDeadBandDegrees)
            desiredLeft = kFullDeflection;
        else if (angleError < -kDeadBandDegrees)
            desiredLeft = -kFullDeflection;

        // Only when stood on top of them: drive forwards to stay there. Sliding off their head is
        // what would otherwise happen, since sideways correction alone cannot hold that position.
        //
        // Proportional here, unlike the sideways component - this is holding a position rather than
        // chasing a bearing, and full throttle would just bounce off the far side.
        float desiredForward = 0.0f;
        if (isAboveTarget(localOrigin, target)) {
            const float yawRadians = viewYaw * trig::kDegreesToRadians;
            const float forward = trig::sine(yawRadians) * deltaY + trig::cosine(yawRadians) * deltaX;
            desiredForward = saturate(forward);
        }

        // BOTH components are written, even when one of them is zero. Writing only leftmove was
        // wrong: the game clears a move field's has-bit whenever that value is zero, so a command
        // steered sideways went out with leftmove present and forwardmove absent - half a movement
        // pair.
        //
        // Skipped for a field the player is already driving themselves, so their own input always
        // wins over ours - also how the reference applies it.
        const auto currentForward = userCmd.forwardMove();
        const auto currentLeft = userCmd.leftMove();
        if (currentForward.hasValue() && currentForward.value() == 0.0f)
            userCmd.setForwardMove(desiredForward);
        if (currentLeft.hasValue() && currentLeft.value() == 0.0f)
            userCmd.setLeftMove(desiredLeft);

        // Hand the same decision to the input-side hook, which is the path that actually moves the
        // player; the command edit above costs nothing and covers the rest.
        pendingForward = desiredForward;
        pendingLeft = desiredLeft;
        hasPendingMove = true;
    }

    [[nodiscard]] static float saturate(float value) noexcept
    {
        if (value > kFullDeflection)
            return kFullDeflection;
        if (value < -kFullDeflection)
            return -kFullDeflection;
        return value;
    }

    [[nodiscard]] static bool isAboveTarget(const cs2::Vector& localOrigin, const Target& target) noexcept
    {
        if (localOrigin.z <= target.eyeZ)
            return false;

        const float deltaX = target.position.x - localOrigin.x;
        const float deltaY = target.position.y - localOrigin.y;
        return deltaX * deltaX + deltaY * deltaY < kOnHeadRadius * kOnHeadRadius;
    }

    // Entity index of the target being held, or kNoTarget. Constant-initialised and trivially
    // destructible, so no __cxa_guard under -nostdlib. An index rather than a pointer on purpose:
    // an entity can be freed between ticks, and a stale pointer would be a use-after-free where a
    // stale index simply fails to resolve.
    inline static int stickyTargetIndex{kNoTarget};

    HookContext& hookContext;
};
