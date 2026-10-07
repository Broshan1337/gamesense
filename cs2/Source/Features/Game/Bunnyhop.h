#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>

#include <CS2/Classes/CCSGOInput.h>
#include <CS2/Classes/CUserCmd.h>
#include <CS2/Classes/Vector.h>
#include <Features/Game/AirStrafe.h>
#include <Features/Game/BunnyhopConfigVariables.h>
#include <Features/Game/Movement.h>
#include <GameClient/ConVars/CvarSystem.h>
#include <GameClient/CSGOInputMovement.h>
#include <GameClient/Entities/BaseEntity.h>
#include <GameClient/Entities/PlayerPawn.h>
#include <GameClient/KeyboardState.h>
#include <GameClient/SubtickMoves.h>
#include <GameClient/Tracing/Tracing.h>
#include <GameClient/UserCmd.h>
#include <HookContext/HookContextMacros.h>
#include <SDL/SdlFunctions.h>
#include <Utils/Optional.h>
#include <Utils/Trig.h>

// Coordinates landing jump taps and view-relative air strafing through the input queue.
// Bunnyhop follows the physical jump key. Auto-strafe is independent and activates in
// the air while jump or movement is held, unless the experimental strafer owns the tick.
template <typename HookContext>
class Bunnyhop {
    // View-relative movement components: x forward, y left, both normalised.
    struct StrafeMove {
        float forward{};
        float left{};
    };

public:
    explicit Bunnyhop(HookContext& hookContext) noexcept
        : hookContext{hookContext}
    {
    }

    // Decides what this tick should do. Runs after the original CreateMove, where the command
    // already carries the player's real input and the pawn's ground state is current. Runs AFTER
    // the Movement feature in the hook order, so the jumpbug's per-tick coordination flag is
    // fresh: while the jumpbug bracket owns this tick's landing, the bunnyhop stands down
    // entirely (friend-source m_bJumpBugActive semantics).
    void onCreateMove(cs2::CUserCmd* cmd) const noexcept
    {
        clearPending();
        if (Movement<HookContext>::jumpBugActive)
            return reset();

        const bool jumpHeld = KeyboardState::isKeyDown(sdl3::scancode::kSpace);
        const bool strafeEnabled = GET_CONFIG_VAR(AutoStrafeEnabled) && !GET_CONFIG_VAR(TestStraferEnabled);
        const bool bhopEnabled = GET_CONFIG_VAR(BunnyhopEnabled) && jumpHeld;
        if (!strafeEnabled && !bhopEnabled)
            return reset();

        const UserCmd userCmd{cmd};
        if (!userCmd)
            return reset();

        auto&& localPawn = hookContext.localPlayerController().pawn().template as<PlayerPawn>();
        if (!localPawn)
            return reset();
        const auto health = localPawn.health();
        const auto walking = isWalking();
        const auto onGround = isOnGround();
        if (!health.hasValue() || health.value() <= 0
            || !walking.hasValue() || !walking.value() || !onGround.hasValue())
            return reset();

        if (onGround.value())
            strafeSide = false;

        // Auto-strafe also works when bhop is disabled or the server handles jumping.
        // Holding movement or jump expresses intent; walking slowly remains manual.
        using Buttons = cs2::CCSGOInput::Buttons;
        constexpr auto moveMask = Buttons::kForward | Buttons::kBack | Buttons::kMoveLeft | Buttons::kMoveRight;
        if (strafeEnabled && !onGround.value() && (jumpHeld || userCmd.isButtonDown(moveMask))
            && !userCmd.isButtonDown(0x10000)) {
            if (const auto move = directionalStrafe(userCmd); move.hasValue()) {
                pendingForward = move.value().forward;
                pendingLeft = move.value().left;
                hasPendingStrafe = true;
                hasPendingInput = true;
            }
        }

        if (!bhopEnabled)
            return;
        const auto autoBhop = hookContext.template make<CvarSystem>().readBoolConVar("sv_autobunnyhopping");
        if (autoBhop.has_value() && autoBhop.value())
            return;

        wantsJump = onGround.value();
        if (!onGround.value()) {
            if (const auto landing = predictLandingFraction(userCmd); landing.hasValue()) {
                pendingLandingWhen = landing.value();
                hasPendingLanding = true;
            }
        }
        hasPendingTap = wantsJump && !hasPendingLanding;
        hasPendingJump = true;
        hasPendingInput = true;
    }

    // Feeds the strafe to the input side, before the game builds the command from it. The jump is
    // NOT applied here - it is a button, and buttons live on the command. forceMove, not setMove:
    // a strafe owns both analog components for its ticks - with the player holding W, setMove's
    // zero-guard would leave forward at 1.0 and cap the wish direction at 45 degrees.
    void onBuildUserCmd(cs2::CCSGOInput* input, int slot) const noexcept
    {
        if (slot != 0 || !hasPendingInput || !hasPendingStrafe)
            return;

        const CSGOInputMovement movement{input};
        if (movement)
            movement.forceMove(pendingForward, pendingLeft);
    }

    // velocity-cs2 never leaves an airborne tick unstrafed: its classic analog airstrafe runs for
    // every tick its quantized strafer did not handle (the hook calls the classic right after the
    // quantized one and skips it only when handled_this_tick is set). This is that fallback for
    // our port: the quantized strafer engages it when IT skips, and the pending statics below make
    // the tick flow through onBuildUserCmd/onWriteMoveCrc like any classic strafe would - those
    // hooks run after CreateMove, where the strafer makes this call. Returns whether the tick was
    // taken over. `requireAutoStrafeToggle` is false for the test-strafer hand-off, which owns its
    // own toggle and must not depend on the classic one being on.
    [[nodiscard]] bool engageClassicStrafe(const UserCmd& userCmd, bool requireAutoStrafeToggle = true) const noexcept
    {
        if (requireAutoStrafeToggle && !GET_CONFIG_VAR(AutoStrafeEnabled))
            return false;

        if (const auto move = directionalStrafe(userCmd); move.hasValue()) {
            pendingForward = move.value().forward;
            pendingLeft = move.value().left;
            hasPendingStrafe = true;
            hasPendingInput = true;
            return true;
        }
        return false;
    }

    // The test strafer's engagement: the caller (which owns the toggle, the ideal-angle math and
    // the side selection) hands over a ready gain angle and side; this only steers the analog pair
    // one angle off the velocity toward the view and flags the tick, so onBuildUserCmd/
    // onWriteMoveCrc carry it through the proven input-side path. `left` picks the swing side.
    [[nodiscard]] bool engageClassicStrafeAngle(const UserCmd& userCmd, float angleDegrees, bool left) const noexcept
    {
        const auto viewYaw = userCmd.viewYaw();
        if (!viewYaw.hasValue() || !std::isfinite(viewYaw.value()) || !std::isfinite(angleDegrees))
            return false;

        const auto velocity = localVelocity();
        if (!velocity.hasValue() || !std::isfinite(velocity.value().x) || !std::isfinite(velocity.value().y))
            return false;

        // Where the player is travelling, relative to where they are looking - the frame the move
        // fields are interpreted in.
        const auto move = air_strafe::moveAtAngle(trig::arcTangent2(velocity.value().y, velocity.value().x),
            viewYaw.value() * trig::kDegreesToRadians, angleDegrees * trig::kDegreesToRadians, left);
        pendingForward = move.forward;
        pendingLeft = move.left;
        hasPendingStrafe = true;
        hasPendingInput = true;
        return true;
    }

    // Presses and releases on the command, just before the game copies its button words into
    // buttons_pb. Also where the jump taps are WRITTEN: slot 7 is the last hook before the
    // command is checksummed and sent, and the one position on this build where appended subtick
    // steps demonstrably reach the wire - slot 6 (BuildUserCmd) rebuilds the subtick timeline
    // from the input queue after CreateMove, so steps appended there were silently dropped (the
    // attack path's press/release pair works from exactly this position; same proof). This is
    // why the hop lost its frame-perfect press and fell back to the banks-only jump.
    void onWriteMoveCrc(cs2::CUserCmd* cmd) const noexcept
    {
        if (!cmd || !hasPendingInput)
            return;

        const UserCmd userCmd{cmd};

        // The jump TAP steps: both the frame-perfect landing pair and the legacy end-of-tick tap
        // land on the command's protobuf here, ordered ahead of the quantized strafer's yaw
        // deltas (which are written later in this same hook and read this timeline's maxWhen).
        if (hasPendingJump) {
            if (hasPendingLanding)
                static_cast<void>(addLandingTap(userCmd, pendingLandingWhen));
            else if (hasPendingTap)
                addJumpTap(userCmd);
            userCmd.setButtonState(cs2::CCSGOInput::Buttons::kJump, wantsJump);
        }

        if (!hasPendingStrafe)
            return;

        using Buttons = cs2::CCSGOInput::Buttons;
        userCmd.setButtonState(Buttons::kForward | Buttons::kBack | Buttons::kMoveLeft | Buttons::kMoveRight, false);

        if (pendingForward > 0.0f)
            userCmd.setButtonState(cs2::CCSGOInput::Buttons::kForward, true);
        else if (pendingForward < 0.0f)
            userCmd.setButtonState(cs2::CCSGOInput::Buttons::kBack, true);

        if (pendingLeft > 0.0f)
            userCmd.setButtonState(cs2::CCSGOInput::Buttons::kMoveLeft, true);
        else if (pendingLeft < 0.0f)
            userCmd.setButtonState(cs2::CCSGOInput::Buttons::kMoveRight, true);
    }

    void onUnload() const noexcept
    {
        reset();
    }

private:
    // A press and a release inside this tick, which is what a real key tap looks like to the
    // sub-tick input system.
    void addJumpTap(const UserCmd& userCmd) const noexcept
    {
        auto&& subtickMoves = hookContext.template make<SubtickMoves>();
        auto* const base = userCmd.baseMessage();

        auto* const press = subtickMoves.add(base, kJumpTapWhen);
        SubtickMoves<HookContext>::setButton(press, cs2::CCSGOInput::Buttons::kJump, true);

        auto* const release = subtickMoves.add(base, kJumpTapWhen);
        SubtickMoves<HookContext>::setButton(release, cs2::CCSGOInput::Buttons::kJump, false);
    }

    // End of the tick, both steps, exactly as the reference does it.
    static constexpr float kJumpTapWhen = 1.0f;

    // The frame-perfect counterpart: release just BEFORE the predicted touchdown, press exactly AT
    // it. The release must come first in the step array - the engine processes steps in order, and
    // the final transition has to be the PRESS, because CS2 jumps on the press edge.
    [[nodiscard]] bool addLandingTap(const UserCmd& userCmd, float when) const noexcept
    {
        auto&& subtickMoves = hookContext.template make<SubtickMoves>();
        auto* const base = userCmd.baseMessage();
        if (!base)
            return false;

        const float releaseWhen = std::clamp(when - 1.0f / 64.0f, 1.0f / 64.0f, 63.0f / 64.0f);
        if (releaseWhen < when) {
            auto* const release = subtickMoves.add(base, releaseWhen);
            if (!release)
                return false;
            SubtickMoves<HookContext>::setButton(release, cs2::CCSGOInput::Buttons::kJump, false);
        }

        auto* const press = subtickMoves.add(base, when);
        if (!press)
            return false;
        SubtickMoves<HookContext>::setButton(press, cs2::CCSGOInput::Buttons::kJump, true);
        return true;
    }

    // Predicts where inside THIS tick's movement the pawn will touch down, as a 0..1 fraction.
    //
    // Ported from velocity-cs2's bhop landing predictor: integrate half a tick of gravity into the
    // current velocity (the game applies gravity across the movement step symmetrically), sweep the
    // pawn's collision hull from its origin along that velocity for one tick minus a small pad, and
    // accept the hit only when the surface it stopped on is actually standable ground. Any failure
    // along the way returns empty - the caller then falls back to the legacy end-of-tick tap rather
    // than jumping at a guessed moment.
    [[nodiscard]] Optional<float> predictLandingFraction(const UserCmd& userCmd) const noexcept
    {
        auto&& localPawn = hookContext.localPlayerController().pawn().template as<PlayerPawn>();
        if (!localPawn)
            return {};
        auto* const pawnEntity = static_cast<cs2::C_BaseEntity*>(localPawn.baseEntity());
        void* const services = movementServices(localPawn);
        if (!services)
            return {};

        const auto origin = localPawn.baseEntity().absOrigin();
        const auto velocity = localVelocity();
        if (!origin.hasValue() || !velocity.hasValue())
            return {};

        // Still rising: nothing can be landed on during this tick.
        if (velocity.value().z > 0.0f)
            return {};

        const auto tickInterval = hookContext.globalVars().tickInterval();
        if (!tickInterval.hasValue())
            return {};

        // The hull, straight off the pawn's embedded collision property.
        const auto collisionOffset = hookContext.schemaSystem().getFieldOffset("C_BaseModelEntity", "m_Collision");
        const auto minsOffset = hookContext.schemaSystem().getFieldOffset("CCollisionProperty", "m_vecMins");
        const auto maxsOffset = hookContext.schemaSystem().getFieldOffset("CCollisionProperty", "m_vecMaxs");
        if (!collisionOffset.has_value() || *collisionOffset <= 0
            || !minsOffset.has_value() || *minsOffset <= 0
            || !maxsOffset.has_value() || *maxsOffset <= 0)
            return {};

        const auto* const collision = reinterpret_cast<const std::byte*>(pawnEntity) + *collisionOffset;
        cs2::Vector mins{};
        cs2::Vector maxs{};
        std::memcpy(&mins, collision + *minsOffset, sizeof(mins));
        std::memcpy(&maxs, collision + *maxsOffset, sizeof(maxs));

        cs2::Vector traceOrigin = origin.value();

        // An in-progress duck is pulling the hull back up mid-air; model the standing hull for the
        // rest of the fall and shift the trace origin down by half of what remains - otherwise the
        // sweep would stop short on the old, lower hull.
        if (userCmd.isButtonDown(kDuckButton)) {
            const auto duckAmount = servicesFloat(services, "m_flDuckAmount");
            if (duckAmount.hasValue() && duckAmount.value() > 0.0f) {
                traceOrigin.z -= (kStandingHeight - maxs.z) * 0.5f;
                maxs.z = kStandingHeight;
            }
        }

        const auto gravityScaleOffset = hookContext.schemaSystem().getFieldOffset("C_BaseEntity", "m_flGravityScale");
        if (!gravityScaleOffset.has_value() || *gravityScaleOffset <= 0)
            return {};
        float gravityScale{};
        std::memcpy(&gravityScale, reinterpret_cast<const std::byte*>(pawnEntity) + *gravityScaleOffset, sizeof(gravityScale));

        const auto svGravity = conVarFloat<cs2::sv_gravity>();
        const auto standableNormal = conVarFloat<cs2::sv_standable_normal>();
        if (!svGravity.hasValue() || !standableNormal.hasValue())
            return {};

        cs2::Vector sweptVelocity = velocity.value();
        sweptVelocity.z -= gravityScale * svGravity.value() * tickInterval.value() * 0.5f;

        cs2::Vector end{};
        end.x = traceOrigin.x + sweptVelocity.x * tickInterval.value();
        end.y = traceOrigin.y + sweptVelocity.y * tickInterval.value();
        end.z = traceOrigin.z + sweptVelocity.z * tickInterval.value() - 2.0f;

        // The landing sweep itself runs through Tracing::traceHull - the same CTraceFilter vtable
        // (0x42E48D8 as of the 2026-08-25 build, was 0x42c4938) the game's own client-side movement prediction builds its filters from, so a
        // correctly-masked world sweep is what this binary's movement code does too. (velocity-cs2
        // routes the same sweep through a movement-services member function, but its signatures are
        // Windows-build-only and do not exist in this Linux binary.)
        const auto result = Tracing::traceHull(traceOrigin, end, mins, maxs, pawnEntity);

        // fraction == 1 means clear air all tick (no touchdown yet); a normal below standable is a
        // ramp or wall graze, not floor to re-jump from.
        if (result.fraction <= 0.0f || result.fraction >= 1.0f || result.normal.z < standableNormal.value())
            return {};

        // Snap to the 1/64 grid (integer math - no libm), clamped away from the exact endpoints so
        // a press at fraction 0 or 1 can never alias into the neighbouring tick's timeline.
        const float grid = result.fraction * 64.0f + 0.5f;
        const float snapped = static_cast<float>(static_cast<int>(grid)) / 64.0f;
        return std::clamp(snapped, 1.0f / 64.0f, 63.0f / 64.0f);
    }

    // m_pMovementServices off the pawn.
    [[nodiscard]] void* movementServices(auto&& localPawn) const noexcept
    {
        const auto servicesOffset = hookContext.schemaSystem().getFieldOffset("C_BasePlayerPawn", "m_pMovementServices");
        if (!servicesOffset.has_value() || *servicesOffset <= 0)
            return nullptr;

        void* services = nullptr;
        std::memcpy(&services, reinterpret_cast<const std::byte*>(static_cast<cs2::C_BaseEntity*>(localPawn.baseEntity())) + *servicesOffset, sizeof(services));
        return services;
    }

    [[nodiscard]] Optional<float> servicesFloat(void* services, const char* fieldName) const noexcept
    {
        if (!services)
            return {};
        const auto fieldOffset = hookContext.schemaSystem().getFieldOffset("CCSPlayer_MovementServices", fieldName);
        if (!fieldOffset.has_value() || *fieldOffset <= 0)
            return {};

        float value{};
        std::memcpy(&value, reinterpret_cast<const std::byte*>(services) + *fieldOffset, sizeof(value));
        return value;
    }



private:
    // Clears everything, so a disabled or unheld feature can never keep pressing or - much worse -
    // keep RELEASING the player's jump.
    void clearPending() const noexcept
    {
        hasPendingInput = false;
        hasPendingJump = false;
        wantsJump = false;
        pendingForward = 0.0f;
        pendingLeft = 0.0f;
        hasPendingStrafe = false;
        hasPendingLanding = false;
        hasPendingTap = false;
    }

    void reset() const noexcept
    {
        clearPending();
        strafeSide = false;
    }

    [[nodiscard]] Optional<StrafeMove> directionalStrafe(const UserCmd& userCmd) const noexcept
    {
        const auto viewYaw = userCmd.viewYaw();
        const auto velocity = localVelocity();
        const auto airMaxWishSpeed = conVarFloat<cs2::sv_air_max_wishspeed>();
        const auto airAccelerate = conVarFloat<cs2::sv_airaccelerate>();
        const auto maxSpeed = conVarFloat<cs2::sv_maxspeed>();
        const auto tickInterval = hookContext.globalVars().tickInterval();
        if (!viewYaw.hasValue() || !velocity.hasValue() || !airMaxWishSpeed.hasValue()
            || !airAccelerate.hasValue() || !maxSpeed.hasValue() || !tickInterval.hasValue())
            return {};
        if (!std::isfinite(viewYaw.value()) || !std::isfinite(velocity.value().x) || !std::isfinite(velocity.value().y))
            return {};

        auto&& localPawn = hookContext.localPlayerController().pawn().template as<PlayerPawn>();
        void* const services = movementServices(localPawn);
        float wishSpeed = maxSpeed.value();
        if (const auto maximum = servicesFloat(services, "m_flMaxspeed"); maximum.hasValue()
            && std::isfinite(maximum.value()) && maximum.value() > 0.0f)
            wishSpeed = std::min(wishSpeed, maximum.value());
        auto&& weapon = localPawn.getActiveWeapon();
        if (const auto maximum = weapon.maxSpeed(); maximum.hasValue()
            && std::isfinite(maximum.value()) && maximum.value() > 0.0f)
            wishSpeed = std::min(wishSpeed, maximum.value());

        const air_strafe::Parameters parameters{wishSpeed, airAccelerate.value(), airMaxWishSpeed.value(),
            tickInterval.value(), servicesFloat(services, "m_flSurfaceFriction").valueOr(1.0f)};
        if (!parameters.valid())
            return {};

        using Buttons = cs2::CCSGOInput::Buttons;
        const air_strafe::Move desired{
            float(userCmd.isButtonDown(Buttons::kForward)) - float(userCmd.isButtonDown(Buttons::kBack)),
            float(userCmd.isButtonDown(Buttons::kMoveLeft)) - float(userCmd.isButtonDown(Buttons::kMoveRight))};
        const auto move = air_strafe::steer(velocity.value().x, velocity.value().y,
            viewYaw.value() * trig::kDegreesToRadians, desired, userCmd.mouseDx().valueOr(0), strafeSide, parameters);
        return StrafeMove{move.forward, move.left};
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

    template <typename ConVarType>
    [[nodiscard]] Optional<float> conVarFloat() const noexcept
    {
        const auto value = hookContext.template make<CvarSystem>().template getConVarValue<ConVarType>();
        if (!value.has_value())
            return {};
        return *value;
    }

    // Below this the velocity direction is meaningless and the angle would be noise.

    // The alternation state for mouse-still strafing (see directionalStrafe): flips every engaged
    // tick so consecutive air accelerations land on opposite sides.
    inline static bool strafeSide{false};

    // MOVETYPE_WALK, from the game's own MOVETYPE name table: the entry naming "MOVETYPE_WALK"
    // carries value 2 (MOVETYPE_NONE is 0 in the same table).
    static constexpr std::uint8_t kMoveTypeWalk = 2;

    // IN_DUCK, 0x4 in the game's own button mask table (attack 0x1, jump 0x2, duck 0x4, forward
    // 0x8 - the anchors the measured masks hang off). The landing predictor needs it because an
    // in-progress duck changes the hull the sweep has to model.
    static constexpr std::uint64_t kDuckButton = 0x4;

    // The standing eye-to-head hull height the reference models a mid-air unduck against.
    static constexpr float kStandingHeight = 72.0f;

    [[nodiscard]] Optional<bool> isWalking() const noexcept
    {
        auto&& localPawn = hookContext.localPlayerController().pawn().template as<PlayerPawn>();
        if (!localPawn)
            return {};

        const auto offset = hookContext.schemaSystem().getFieldOffset("C_BaseEntity", "m_MoveType");
        if (!offset.has_value() || *offset <= 0)
            return {};

        std::uint8_t moveType{};
        std::memcpy(&moveType, reinterpret_cast<const std::byte*>(static_cast<cs2::C_BaseEntity*>(localPawn.baseEntity())) + *offset, sizeof(moveType));
        return moveType == kMoveTypeWalk;
    }

    [[nodiscard]] Optional<bool> isOnGround() const noexcept
    {
        auto&& localPawn = hookContext.localPlayerController().pawn().template as<PlayerPawn>();
        if (!localPawn)
            return {};

        const auto offset = hookContext.schemaSystem().getFieldOffset("C_BaseEntity", "m_fFlags");
        if (!offset.has_value() || *offset <= 0)
            return {};

        std::uint32_t flags{};
        std::memcpy(&flags, reinterpret_cast<const std::byte*>(static_cast<cs2::C_BaseEntity*>(localPawn.baseEntity())) + *offset, sizeof(flags));
        return (flags & kOnGroundFlag) != 0;
    }

    // FL_ONGROUND, read out of the game's own flag-name table rather than assumed: the entry naming
    // "FL_ONGROUND" carries mask 0x1.
    static constexpr std::uint32_t kOnGroundFlag = 0x1;

    // Constant-initialised and trivially destructible, so no __cxa_guard under -nostdlib.
    inline static bool hasPendingInput{false};
    inline static bool hasPendingJump{false};
    inline static bool wantsJump{false};
    inline static float pendingForward{0.0f};
    inline static float pendingLeft{0.0f};
    inline static bool hasPendingStrafe{false};
    // True when this tick's command will carry the frame-perfect landing pair (release +
    // press at the predicted touchdown fraction); suppresses the legacy end-of-tick tap.
    inline static bool hasPendingLanding{false};
    // The predicted touchdown fraction the landing pair is written at (computed at CreateMove,
    // consumed by the slot-7 write).
    inline static float pendingLandingWhen{0.5f};
    // True when the legacy tap will be written (grounded tick, or prediction unavailable).
    inline static bool hasPendingTap{false};

    HookContext& hookContext;
};
