#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>

#include <CS2/Classes/CCSGOInput.h>
#include <CS2/Classes/CUserCmd.h>
#include <CS2/Classes/Vector.h>
#include <Features/Game/AirStrafe.h>
#include <Features/Game/JumpTiming.h>
#include <Utils/VerifyConsole.h>
#include <Features/Game/BunnyhopConfigVariables.h>
#include <Features/Game/Movement.h>
#include <Features/Game/StrafeCommand.h>
#include <GameClient/ConVars/CvarSystem.h>
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










template <typename HookContext>
class Bunnyhop {
    
    struct StrafeMove {
        float forward{};
        float left{};
    };

public:
    explicit Bunnyhop(HookContext& hookContext) noexcept
        : hookContext{hookContext}
    {
    }

    
    
    
    
    
    void onCreateMove(cs2::CUserCmd* cmd) const noexcept
    {
        clearPending();
        if (Movement<HookContext>::jumpBugActive) {
            VerifyConsole::write(4.0f, "bhop", "gate jumpBugActive");
            return reset();
        }

        const bool jumpHeld = KeyboardState::isKeyDown(sdl3::scancode::kSpace);
        // Issue #4 round 4: [bhop] breadcrumbs at every gate so the next
        // in-game run names the failing one ("only some ticks" needs data).
        VerifyConsole::write(4.0f, "bhop", "cm held=%d", jumpHeld ? 1 : 0);
        const bool strafeEnabled = GET_CONFIG_VAR(AutoStrafeEnabled) || GET_CONFIG_VAR(TestStraferEnabled);
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

        pendingCommand = cmd;
        pendingSequence = StrafeCommand::commandNumber(cmd);
        pendingBase = userCmd.baseMessage();
        if (onGround.value())
            strafeSide = false;

        // Auto-strafe also works when bhop is disabled or the server handles jumping.
        // Holding movement or jump expresses intent; walking slowly remains manual.
        using Buttons = cs2::CCSGOInput::Buttons;
        constexpr auto moveMask = Buttons::kForward | Buttons::kBack | Buttons::kMoveLeft | Buttons::kMoveRight;
        if (strafeEnabled && !onGround.value() && (jumpHeld || userCmd.isButtonDown(Buttons::kJump | moveMask)
                || std::abs(userCmd.forwardMove().valueOr(0.0f)) > 0.001f
                || std::abs(userCmd.leftMove().valueOr(0.0f)) > 0.001f)
            && !userCmd.isButtonDown(0x10000)) {
            // steer() flips the side on every call, but CreateMove runs every
            // frame and only the last frame of a command is sent: every frame of
            // one command starts from the side the previous command ended on.
            if (const int sequence = StrafeCommand::commandNumber(cmd); sequence != strafeSequence) {
                strafeSequence = sequence;
                strafeSideBase = strafeSide;
            }
            strafeSide = strafeSideBase;
            if (const auto move = directionalStrafe(userCmd); move.hasValue()) {
                hasPendingInput = pendingStrafe.stage(cmd, {move.value().forward, move.value().left});
            }
        }

        if (!bhopEnabled)
            return;
        const auto autoBhop = hookContext.template make<CvarSystem>().readBoolConVar("sv_autobunnyhopping");
        if (autoBhop.has_value() && autoBhop.value()) {
            VerifyConsole::write(4.0f, "bhop", "gate sv_autobunnyhopping=1 (server-side hop)");
            return;
        }

        // Velocity gate: ladder/noclip movetypes stand down (m_nActualMoveType:
        // ladder=9, noclip=7 - the CS2 MoveType_t from the dumper).
        if (const auto moveType = pawnMoveType(); moveType == 9 || moveType == 7) {
            VerifyConsole::write(4.0f, "bhop", "gate movetype=%d", moveType);
            return reset();
        }

        wantsJump = onGround.value();
        if (!onGround.value()) {
            if (const auto landing = predictLandingFraction(userCmd); landing.hasValue()) {
                pendingLandingWhen = landing.value();
                hasPendingLanding = true;
            }
        }
        // This is only the per-FRAME candidate (CreateMove runs every frame on the
        // in-progress command). The decision that is actually sent is latched
        // once per command in onWriteMoveCrc.
        // A predicted landing stages the press at the landing subtick (not the
        // next ground tick at when=1.0) - that keeps the hop frictionless.
        wantsJump = onGround.value() || hasPendingLanding;
        VerifyConsole::write(4.0f, "bhop", "ground=%d landing=%d -> wantsJump=%d",
            onGround.value() ? 1 : 0, hasPendingLanding ? 1 : 0, wantsJump ? 1 : 0);
        hasPendingTap = wantsJump;
        hasPendingJump = true;
        hasPendingInput = true;
    }

    
    
    
    
    void onWriteMoveCrc(cs2::CUserCmd* cmd) const noexcept
    {
        // The game rebuilds the command from the player's real input every
        // frame, so this is the true movement intent - captured here, before
        // the strafe commit below overwrites the scalars and the move buttons.
        if (const UserCmd real{cmd}; real) {
            using Buttons = cs2::CCSGOInput::Buttons;
            playerMove = {real.forwardMove().valueOr(0.0f), real.leftMove().valueOr(0.0f)};
            if (std::abs(playerMove.forward) < 0.001f && std::abs(playerMove.left) < 0.001f)
                playerMove = {
                    float(real.isButtonDown(Buttons::kForward)) - float(real.isButtonDown(Buttons::kBack)),
                    float(real.isButtonDown(Buttons::kMoveLeft)) - float(real.isButtonDown(Buttons::kMoveRight))};
        }
        if (!hasPendingInput) {
            VerifyConsole::write(4.0f, "bhop", "crc: no pending input");
            return;
        }
        const UserCmd userCmd{cmd};
        // Issue #4 (2026-10-13, user breadcrumb): NO cross-hook cmd identity -
        // CreateMove stages on its cmd (ring entry), WriteMoveCrc runs on a
        // DIFFERENT cmd (the send-built one, 0x98 = one array slot away); the
        // old pointer/base/sequence check discarded every staged jump ("fires
        // 1-2 hops then stops" - only the coincidental same-pointer ticks
        // fired). The tick boundary is already guaranteed by clearPending()
        // at the top of onCreateMove; this writes ONLY the cmd handed here.
        if (!cmd || Movement<HookContext>::jumpBugActive) {
            VerifyConsole::write(4.0f, "bhop", "crc: gate (no cmd / jumpbug)");
            clearPending();
            return;
        }
        auto&& localPawn = hookContext.localPlayerController().pawn().template as<PlayerPawn>();
        const auto walking = isWalking();
        if (!localPawn || localPawn.health().valueOr(0) <= 0 || !walking.valueOr(false)) {
            clearPending();
            return;
        }

        
        
        
        if (hasPendingJump && GET_CONFIG_VAR(BunnyhopEnabled)) {
            using Moves = SubtickMoves<HookContext>;
            // CreateMove and WriteMoveCrc run every FRAME on the in-progress
            // command (~4 frames per tick at 250 fps), and the game rebuilds
            // that command from its own input each frame: only the LAST
            // frame's write is sent. A decision that changes between the
            // frames of one command (the old per-frame press, then release)
            // only went out when the press happened to sit on the last frame:
            // "holding space jumps rarely" (per-frame file trace, 2026-10-12:
            // every hop that fired had its press on the command's final frame).
            // So the jump is latched once per command and re-applied
            // identically on every frame of it.
            //  - spacing: a press is at least two commands after the previous
            //    one, which also clears sv_jump_spam_penalty_time (one tick).
            //  - a landing press (when < 1.0) only latches on the command's
            //    first frame: its fraction was measured on the previous frame,
            //    so it can only be late (already on ground), never early.
            //  - a ground press (when = 1.0) may latch on any frame.
            //  - once latched for a command, a jump is never taken back.
            // Airborne commands carry the release in the button banks only;
            // the game's own real jump samples are never stripped.
            const int sequence = StrafeCommand::commandNumber(cmd);
            const bool firstFrame = sequence != latchedSequence;
            if (firstFrame) {
                latchedSequence = sequence;
                latchedJump = false;
            }
            const bool spaced = sequence < lastPressSequence || sequence - lastPressSequence >= 2;
            if (!latchedJump && wantsJump && spaced && (firstFrame || !hasPendingLanding)) {
                latchedJump = true;
                latchedWhen = hasPendingLanding ? pendingLandingWhen : 1.0f;
                lastPressSequence = sequence;
            }
            userCmd.replaceButtons(cs2::CCSGOInput::Buttons::kJump,
                latchedJump ? cs2::CCSGOInput::Buttons::kJump : 0);
            if (!latchedJump) {
                clearPending();
                return;
            }
            if (Moves::count(userCmd.baseMessage()) + 2
                > cs2::CUserCmd::BaseMessage::SubtickMoves::kMaxSteps)
                return clearPending();
            auto&& moves = hookContext.template make<SubtickMoves>();
            auto* const press = moves.add(userCmd.baseMessage(), latchedWhen);
            if (press) {
                Moves::setButton(press, cs2::CCSGOInput::Buttons::kJump, true);
                if (auto* release = moves.add(userCmd.baseMessage(), latchedWhen)) {
                    Moves::setButton(release, cs2::CCSGOInput::Buttons::kJump, false);
                } else {
                    Moves::stripButtons(userCmd.baseMessage(), cs2::CCSGOInput::Buttons::kJump);
                }
                // The pair is appended after the game's real samples while a
                // landing when is < 1.0: the engine walks the step array
                // sequentially, so it must be re-sorted (stuck keys class).
                Moves::sortByWhen(userCmd.baseMessage());
            }
        }

        if (pendingStrafe.active() && isOnGround() == false
            && (GET_CONFIG_VAR(AutoStrafeEnabled) || GET_CONFIG_VAR(TestStraferEnabled)))
            static_cast<void>(pendingStrafe.template commit<HookContext>(cmd));
        clearPending();
    }

    void onUnload() const noexcept
    {
        reset();
    }

private:
    
    
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

        
        if (velocity.value().z > 0.0f)
            return {};

        const auto tickInterval = hookContext.globalVars().tickInterval();
        if (!tickInterval.hasValue())
            return {};

        
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

        
        
        
        
        
        const auto result = Tracing::traceHull(traceOrigin, end, mins, maxs, pawnEntity);

        
        
        if (!result.valid || result.fraction <= 0.0f || result.fraction >= 1.0f || result.normal.z < standableNormal.value())
            return {};

        
        
        return jump_timing::landingWhen(result.fraction);
    }

    
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
    
    
    void clearPending() const noexcept
    {
        hasPendingInput = false;
        hasPendingJump = false;
        wantsJump = false;
        pendingStrafe.reset();
        pendingCommand = nullptr;
        pendingBase = nullptr;
        hasPendingLanding = false;
        hasPendingTap = false;
    }

    void reset() const noexcept
    {
        clearPending();
        strafeSide = false;
        jumpTiming.reset();
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
        if (userCmd.isButtonDown(cs2::CCSGOInput::Buttons::kAttack)) {
            if (const auto factor = weapon.attackMovespeedFactor(); factor.hasValue()
                && std::isfinite(factor.value()) && factor.value() > 0.0f && factor.value() <= 1.0f)
                wishSpeed *= factor.value();
        }

        const air_strafe::Parameters parameters{wishSpeed, airAccelerate.value(), airMaxWishSpeed.value(),
            tickInterval.value(), servicesFloat(services, "m_flSurfaceFriction").valueOr(1.0f)};
        if (!parameters.valid())
            return {};

        // CreateMove runs after WriteMoveCrc on the same command, so the command's
        // own scalars and buttons already hold the previous strafe commit here.
        // Steering toward them made the strafer chase its own output (constant
        // left/right flicker once the strafe actually committed, e.g. on
        // sv_autobunnyhopping servers). Use the intent captured at WriteMoveCrc.
        const air_strafe::Move desired = playerMove;
        const int mouseDx = userCmd.mouseDx().valueOr(0);
        const bool legit = static_cast<std::uint8_t>(GET_CONFIG_VAR(AutoStrafeMode)) == 0;
        if (legit && std::abs(mouseDx) < static_cast<int>(GET_CONFIG_VAR(LegitStrafeMouseThreshold))) return {};
        const auto move = air_strafe::steer(velocity.value().x, velocity.value().y,
            viewYaw.value() * trig::kDegreesToRadians, desired, userCmd.mouseDx().valueOr(0), strafeSide, parameters);
        if (legit) {
            // Mouse controls the strafe side; keep manual direction and blend
            // assistance without switching sides on a stationary crosshair.
            const auto assisted = air_strafe::moveAtAngle(std::atan2(velocity.value().y, velocity.value().x),
                viewYaw.value() * trig::kDegreesToRadians,
                air_strafe::idealAngle(std::hypot(velocity.value().x, velocity.value().y), parameters), mouseDx < 0);
            const float blend = static_cast<float>(GET_CONFIG_VAR(LegitStrafeStrength)) / 100;
            if (blend <= 0) return {};
            return StrafeMove{desired.forward * (1 - blend) + assisted.forward * blend,
                desired.left * (1 - blend) + assisted.left * blend};
        }
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

    

    
    
    inline static bool strafeSide{false};
    inline static bool strafeSideBase{false};
    inline static int strafeSequence{-1};
    inline static air_strafe::Move playerMove{};
    static constexpr std::uint8_t kMoveTypeWalk = 2;

    
    
    
    static constexpr std::uint64_t kDuckButton = 0x4;

    
    static constexpr float kStandingHeight = 72.0f;

    [[nodiscard]] int pawnMoveType() const noexcept
    {
        auto&& localPawn = hookContext.localPlayerController().pawn().template as<PlayerPawn>();
        if (!localPawn)
            return -1;
        const auto offset = hookContext.schemaSystem().getFieldOffset("C_BaseEntity", "m_nActualMoveType");
        if (!offset.has_value() || *offset <= 0)
            return -1;
        std::uint8_t moveType{};
        std::memcpy(&moveType, reinterpret_cast<const std::byte*>(static_cast<cs2::C_BaseEntity*>(localPawn.baseEntity())) + *offset, sizeof(moveType));
        return moveType;
    }

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

    
    
    static constexpr std::uint32_t kOnGroundFlag = 0x1;

    
    inline static int latchedSequence{-1};
    inline static bool latchedJump{false};
    inline static float latchedWhen{1.0f};
    inline static int lastPressSequence{-1000};
    inline static jump_timing::State jumpTiming;
    inline static double pendingJumpTime{};
    inline static bool hasPendingInput{false};
    inline static bool hasPendingJump{false};
    inline static bool wantsJump{false};
    inline static StrafeCommand pendingStrafe;
    inline static cs2::CUserCmd* pendingCommand{};
    inline static std::byte* pendingBase{};
    inline static int pendingSequence{};
    
    
    inline static bool hasPendingLanding{false};
    
    
    inline static float pendingLandingWhen{0.5f};
    
    inline static bool hasPendingTap{false};

    HookContext& hookContext;
};
