#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>

#include <CS2/Classes/CCSGOInput.h>
#include <CS2/Classes/CUserCmd.h>
#include <CS2/Classes/Vector.h>
#include <Features/Game/MovementConfigVariables.h>
#include <GameClient/ConVars/CvarSystem.h>
#include <GameClient/CSGOInputMovement.h>
#include <GameClient/Entities/BaseEntity.h>
#include <GameClient/Entities/PlayerPawn.h>
#include <GameClient/SubtickMoves.h>
#include <GameClient/Tracing/Tracing.h>
#include <GameClient/UserCmd.h>
#include <HookContext/HookContextMacros.h>
#include <Utils/Optional.h>
#include <Utils/Trig.h>
















template <typename HookContext>
class Movement {
public:
    explicit Movement(HookContext& hookContext) noexcept
        : hookContext{hookContext}
    {
    }

    void onCreateMove(cs2::CUserCmd* cmd) const noexcept
    {
        reset();

        if (!GET_CONFIG_VAR(movement_vars::EdgeJump)
            && !GET_CONFIG_VAR(movement_vars::EdgeStop)
            && !GET_CONFIG_VAR(movement_vars::SlowWalk)
            && !GET_CONFIG_VAR(movement_vars::FastLadder)
            && !GET_CONFIG_VAR(movement_vars::JumpBug))
            return;

        const UserCmd userCmd{cmd};
        if (!userCmd)
            return;

        auto&& localPawn = hookContext.localPlayerController().pawn().template as<PlayerPawn>();
        if (!localPawn)
            return;

        const auto health = localPawn.health();
        if (!health.hasValue() || health.value() <= 0)
            return;

        
        if (GET_CONFIG_VAR(movement_vars::SlowWalk)) {
            const auto flags = entityFlags(localPawn);
            const auto moveType = moveTypeOf(localPawn);
            if (flags.hasValue() && (flags.value() & kOnGroundFlag)
                && moveType.hasValue() && moveType.value() == kMoveTypeWalk) {
                const auto forward = userCmd.forwardMove();
                const auto left = userCmd.leftMove();
                if (forward.hasValue() && left.hasValue()) {
                    const float moveLength = trig::squareRoot(forward.value() * forward.value() + left.value() * left.value());
                    if (moveLength > 0.001f) {
                        const float targetRatio = static_cast<float>(GET_CONFIG_VAR(movement_vars::SlowWalkSpeed)) / 100.0f;
                        const float scaled = std::min(moveLength, targetRatio);
                        pendingForward = (forward.value() / moveLength) * scaled;
                        pendingLeft = (left.value() / moveLength) * scaled;
                        pendingForceMove = true;
                    }
                }
            }
        }

        
        if (GET_CONFIG_VAR(movement_vars::FastLadder)) {
            const auto moveType = moveTypeOf(localPawn);
            if (moveType.hasValue() && moveType.value() == kMoveTypeLadder) {
                const auto forward = userCmd.forwardMove();
                const auto left = userCmd.leftMove();
                const auto viewYaw = userCmd.viewYaw();
                const auto viewPitch = userCmd.viewPitch();
                if (forward.hasValue() && left.hasValue() && (forward.value() != 0.0f || left.value() != 0.0f)
                    && viewYaw.hasValue() && viewPitch.hasValue()) {

                    
                    
                    bool goingUp = false;
                    if (forward.value() > 0.01f || forward.value() < -0.01f)
                        goingUp = forward.value() > 0.0f;
                    else
                        goingUp = viewPitch.value() < 0.0f;

                    
                    
                    
                    
                    float modifiedYaw = viewYaw.value() + (goingUp ? -90.0f : 90.0f);
                    while (modifiedYaw > 180.0f)
                        modifiedYaw -= 360.0f;
                    while (modifiedYaw < -180.0f)
                        modifiedYaw += 360.0f;

                    pendingForward = -1.0f;
                    pendingLeft = goingUp ? 1.0f : -1.0f;
                    pendingForceMove = true;
                    pendingClearMoveButtons = true;
                    pendingBackButton = true;
                    pendingLeftButton = goingUp;
                    pendingRightButton = !goingUp;
                    pendingViewPitch = 89.0f;
                    pendingViewYaw = modifiedYaw;
                    pendingViewAngles = true;
                }
            }
        }

        
        const bool edgeJump = GET_CONFIG_VAR(movement_vars::EdgeJump);
        const bool edgeStop = GET_CONFIG_VAR(movement_vars::EdgeStop) && !edgeJump;
        if (edgeJump || edgeStop) {
            const auto flags = entityFlags(localPawn);
            const auto moveType = moveTypeOf(localPawn);
            if (flags.hasValue() && (flags.value() & kOnGroundFlag)
                && moveType.hasValue() && moveType.value() == kMoveTypeWalk) {

                const auto velocity = localVelocity(localPawn);
                const auto speed2d = velocity.hasValue()
                    ? trig::squareRoot(velocity.value().x * velocity.value().x + velocity.value().y * velocity.value().y)
                    : 0.0f;

                if (edgeJump && speed2d >= 1.0f) {
                    if (const auto origin = localPawn.baseEntity().absOrigin(); origin.hasValue()) {
                        
                        
                        
                        const bool currentOnEdge = checkEdge(localPawn, origin.value(), velocity.value(), 1);
                        const bool nextOnEdge = checkEdge(localPawn, origin.value(), velocity.value(), 2);
                        if (nextOnEdge && !currentOnEdge
                            && !groundAheadHasStairStep(localPawn, origin.value(), velocity.value(), 1)
                            && !groundAheadHasStairStep(localPawn, origin.value(), velocity.value(), 2))
                            pendingEdgeJump = true;
                    }
                }

                if (edgeStop && !pendingEdgeJump) {
                    if (const auto origin = localPawn.baseEntity().absOrigin(); origin.hasValue()) {
                        const bool atEdge = checkEdge(localPawn, origin.value(), velocity.value(), 1);
                        bool approachingEdge = false;
                        if (!atEdge && speed2d > 1.0f) {
                            
                            
                            
                            const auto friction = conVarFloat<cs2::sv_friction>();
                            const auto stopSpeed = conVarFloat<cs2::sv_stopspeed>();
                            const auto tickInterval = hookContext.globalVars().tickInterval();
                            if (friction.hasValue() && stopSpeed.hasValue() && tickInterval.hasValue() && friction.value() > 0.0f) {
                                const float decelPerTick = friction.value() * std::max(speed2d, stopSpeed.value()) * tickInterval.value();
                                const int ticksNeeded = std::max(2, static_cast<int>((speed2d / decelPerTick) + 0.5f));
                                for (int i = 2; i <= ticksNeeded + 2; ++i) {
                                    if (checkEdge(localPawn, origin.value(), velocity.value(), i)) {
                                        approachingEdge = true;
                                        break;
                                    }
                                }
                            }
                        }

                        if (atEdge || approachingEdge) {
                            const auto viewYaw = userCmd.viewYaw();
                            if (viewYaw.hasValue()) {
                                
                                
                                float weaponMaxSpeed = 250.0f;
                                if (const auto maxSpeed = localPawn.getActiveWeapon().maxSpeed(); maxSpeed.hasValue() && maxSpeed.value() > 0.0f)
                                    weaponMaxSpeed = maxSpeed.value();

                                const auto accelerate = conVarFloat<cs2::sv_accelerate>();
                                const auto tickInterval = hookContext.globalVars().tickInterval();
                                float moveRatio = 1.0f;
                                if (speed2d > 1.0f && accelerate.hasValue() && tickInterval.hasValue()) {
                                    const float accelSpeed = accelerate.value() * weaponMaxSpeed * tickInterval.value();
                                    moveRatio = std::clamp(speed2d < accelSpeed ? speed2d / weaponMaxSpeed : 1.0f, 0.0f, 1.0f);
                                }

                                const float stopYaw = trig::arcTangent2(velocity.value().y, velocity.value().x) * trig::kRadiansToDegrees + 180.0f;
                                const float rotation = (viewYaw.value() - stopYaw) * trig::kDegreesToRadians;
                                pendingForward = std::clamp(trig::cosine(rotation) * moveRatio, -1.0f, 1.0f);
                                pendingLeft = std::clamp(trig::sine(rotation) * moveRatio * -1.0f, -1.0f, 1.0f);

                                
                                
                                
                                
                                
                                if (atEdge) {
                                    const auto forward = userCmd.forwardMove();
                                    const auto left = userCmd.leftMove();
                                    bool pushBack = false;
                                    float pushbackRotation = 0.0f;
                                    if (forward.hasValue() && left.hasValue()) {
                                        const float yawRadians = viewYaw.value() * trig::kDegreesToRadians;
                                        const float wishX = trig::cosine(yawRadians) * forward.value() - trig::sine(yawRadians) * left.value();
                                        const float wishY = trig::sine(yawRadians) * forward.value() + trig::cosine(yawRadians) * left.value();
                                        const float wishLength = trig::squareRoot(wishX * wishX + wishY * wishY);
                                        if (wishLength > 0.001f) {
                                            const float wishXNorm = wishX / wishLength;
                                            const float wishYNorm = wishY / wishLength;

                                            const cs2::Vector testOrigin{
                                                origin.value().x + wishXNorm * 4.0f,
                                                origin.value().y + wishYNorm * 4.0f,
                                                origin.value().z};
                                            const bool inputTowardEdge = checkEdgeAt(localPawn, testOrigin);
                                            if (inputTowardEdge) {
                                                pushBack = true;
                                                const float pushbackYaw = trig::arcTangent2(wishYNorm, wishXNorm) * trig::kRadiansToDegrees + 180.0f;
                                                pushbackRotation = (viewYaw.value() - pushbackYaw) * trig::kDegreesToRadians;
                                            }
                                        }
                                    }

                                    if (pushBack) {
                                        pendingForward = std::clamp(trig::cosine(pushbackRotation) * 0.3f, -1.0f, 1.0f);
                                        pendingLeft = std::clamp(trig::sine(pushbackRotation) * -0.3f, -1.0f, 1.0f);
                                        
                                        pendingBackButton = pendingForward < 0.0f;
                                        pendingForwardButton = pendingForward > 0.0f;
                                        pendingLeftButton = pendingLeft > 0.0f;
                                        pendingRightButton = pendingLeft < 0.0f;
                                        pendingForceMove = pendingClearMoveButtons = true;
                                        hasPendingInput = true;
                                        if (speed2d <= 5.0f)
                                            return;
                                    }
                                }

                                pendingForceMove = true;
                                pendingClearMoveButtons = true;
                                pendingBackButton = pendingForward < 0.0f;
                                pendingForwardButton = pendingForward > 0.0f;
                                pendingLeftButton = pendingLeft > 0.0f;
                                pendingRightButton = pendingLeft < 0.0f;
                            }
                        }
                    }
                }
            }
        }

        
        if (GET_CONFIG_VAR(movement_vars::JumpBug)) {
            const auto flags = entityFlags(localPawn);
            const auto moveType = moveTypeOf(localPawn);
            if (flags.hasValue() && !(flags.value() & kOnGroundFlag)
                && moveType.hasValue() && moveType.value() == kMoveTypeWalk
                && userCmd.isButtonDown(kJumpButton)) {

                const auto velocity = localVelocity(localPawn);
                if (velocity.hasValue() && velocity.value().z <= 0.0f) {
                    if (const auto landing = predictLanding(localPawn, userCmd, velocity.value()); landing.hasValue()) {
                        pendingJumpBugWhen = landing.value();
                        pendingJumpBug = true;
                        
                        
                        
                        
                        
                        jumpBugActive = true;
                    }
                }
            }
        }

        hasPendingInput = pendingForceMove || pendingEdgeJump || pendingJumpBug
            || pendingClearMoveButtons || pendingViewAngles;
    }

    void onBuildUserCmd(cs2::CCSGOInput* input, int slot) const noexcept
    {
        if (slot != 0 || !hasPendingInput || !pendingForceMove)
            return;

        const CSGOInputMovement movement{input};
        if (movement)
            movement.forceMove(pendingForward, pendingLeft);
    }

    void onWriteMoveCrc(cs2::CUserCmd* cmd) const noexcept
    {
        if (!cmd || !hasPendingInput)
            return;

        const UserCmd userCmd{cmd};

        
        
        if (pendingEdgeJump)
            userCmd.setButtonState(cs2::CCSGOInput::Buttons::kJump, true);

        
        
        
        if (pendingClearMoveButtons) {
            constexpr auto kMoveMask = cs2::CCSGOInput::Buttons::kForward | cs2::CCSGOInput::Buttons::kBack
                | cs2::CCSGOInput::Buttons::kMoveLeft | cs2::CCSGOInput::Buttons::kMoveRight;
            userCmd.setButtonState(kMoveMask, false);
            if (pendingForwardButton)
                userCmd.setButtonState(cs2::CCSGOInput::Buttons::kForward, true);
            if (pendingBackButton)
                userCmd.setButtonState(cs2::CCSGOInput::Buttons::kBack, true);
            if (pendingLeftButton)
                userCmd.setButtonState(cs2::CCSGOInput::Buttons::kMoveLeft, true);
            if (pendingRightButton)
                userCmd.setButtonState(cs2::CCSGOInput::Buttons::kMoveRight, true);
        }

        
        if (pendingViewAngles)
            userCmd.setViewAngles(pendingViewPitch, pendingViewYaw);

        
        
        
        
        if (pendingJumpBug) {
            auto&& subtickMoves = hookContext.template make<SubtickMoves>();
            auto* const base = userCmd.baseMessage();

            auto* const duckDown = subtickMoves.add(base, 0.0f);
            if (duckDown)
                SubtickMoves<HookContext>::setButton(duckDown, kDuckButton, true);

            auto* const duckUp = subtickMoves.add(base, pendingJumpBugWhen);
            if (duckUp)
                SubtickMoves<HookContext>::setButton(duckUp, kDuckButton, false);

            auto* const jumpUp = subtickMoves.add(base, pendingJumpBugWhen);
            if (jumpUp)
                SubtickMoves<HookContext>::setButton(jumpUp, cs2::CCSGOInput::Buttons::kJump, false);

            auto* const jumpDown = subtickMoves.add(base, pendingJumpBugWhen);
            if (jumpDown)
                SubtickMoves<HookContext>::setButton(jumpDown, cs2::CCSGOInput::Buttons::kJump, true);
        }
    }

    void onUnload() const noexcept
    {
        reset();
    }

    
    
    
    inline static bool jumpBugActive{false};

private:
    void reset() const noexcept
    {
        hasPendingInput = false;
        pendingForceMove = false;
        pendingForward = 0.0f;
        pendingLeft = 0.0f;
        pendingEdgeJump = false;
        pendingJumpBug = false;
        pendingJumpBugWhen = 0.5f;
        pendingClearMoveButtons = false;
        pendingForwardButton = false;
        pendingBackButton = false;
        pendingLeftButton = false;
        pendingRightButton = false;
        pendingViewAngles = false;
        pendingViewPitch = 0.0f;
        pendingViewYaw = 0.0f;
        jumpBugActive = false;
    }

    
    
    
    [[nodiscard]] bool checkEdge(auto&& localPawn, const cs2::Vector& origin, const cs2::Vector& velocity, int ticksAhead) const noexcept
    {
        const auto tickInterval = hookContext.globalVars().tickInterval();
        if (!tickInterval.hasValue())
            return false;

        const auto standableNormal = conVarFloat<cs2::sv_standable_normal>();
        if (!standableNormal.hasValue())
            return false;

        const auto hull = collisionHull(localPawn);
        if (!hull.hasValue())
            return false;

        auto* const pawnEntity = static_cast<cs2::C_BaseEntity*>(localPawn.baseEntity());
        const float bt = tickInterval.value() * static_cast<float>(ticksAhead);
        const cs2::Vector predicted{origin.x + velocity.x * bt, origin.y + velocity.y * bt, origin.z};
        const cs2::Vector start{predicted.x, predicted.y, predicted.z + 2.0f};
        const cs2::Vector end{predicted.x, predicted.y, predicted.z - 4.0f};

        const auto result = Tracing::traceHull(start, end, hull.value().mins, hull.value().maxs, pawnEntity);
        return result.fraction >= 1.0f || result.normal.z < standableNormal.value();
    }

    
    
    [[nodiscard]] bool checkEdgeAt(auto&& localPawn, const cs2::Vector& position) const noexcept
    {
        const auto standableNormal = conVarFloat<cs2::sv_standable_normal>();
        if (!standableNormal.hasValue())
            return false;

        const auto hull = collisionHull(localPawn);
        if (!hull.hasValue())
            return false;

        auto* const pawnEntity = static_cast<cs2::C_BaseEntity*>(localPawn.baseEntity());
        const cs2::Vector start{position.x, position.y, position.z + 2.0f};
        const cs2::Vector end{position.x, position.y, position.z - 4.0f};

        const auto result = Tracing::traceHull(start, end, hull.value().mins, hull.value().maxs, pawnEntity);
        return result.fraction >= 1.0f || result.normal.z < standableNormal.value();
    }

    
    
    
    [[nodiscard]] bool groundAheadHasStairStep(auto&& localPawn, const cs2::Vector& origin, const cs2::Vector& velocity, int ticksAhead) const noexcept
    {
        const auto tickInterval = hookContext.globalVars().tickInterval();
        if (!tickInterval.hasValue())
            return false;

        const auto standableNormal = conVarFloat<cs2::sv_standable_normal>();
        if (!standableNormal.hasValue())
            return false;

        const auto hull = collisionHull(localPawn);
        if (!hull.hasValue())
            return false;

        auto* const pawnEntity = static_cast<cs2::C_BaseEntity*>(localPawn.baseEntity());
        const float speed = trig::squareRoot(velocity.x * velocity.x + velocity.y * velocity.y);
        if (speed < 0.01f)
            return false;

        const float perpX = -velocity.y / speed;
        const float perpY = velocity.x / speed;
        constexpr float kLateral[]{0.0f, 7.0f, -7.0f, 14.0f, -14.0f, 22.0f, -22.0f, 34.0f, -34.0f};

        const float bt = tickInterval.value() * static_cast<float>(ticksAhead);
        const float baseX = origin.x + velocity.x * bt;
        const float baseY = origin.y + velocity.y * bt;

        for (const float lateral : kLateral) {
            const float wx = baseX + perpX * lateral;
            const float wy = baseY + perpY * lateral;
            const cs2::Vector start{wx, wy, origin.z + 2.0f};
            const cs2::Vector end{wx, wy, origin.z - 110.0f};

            const auto trace = Tracing::traceHull(start, end, hull.value().mins, hull.value().maxs, pawnEntity);
            if (trace.fraction >= 1.0f || trace.normal.z < standableNormal.value())
                continue;

            const float stepDown = origin.z - trace.endPos.z;
            if (stepDown >= 0.12f && stepDown <= 48.0f)
                return true;
        }
        return false;
    }

    
    
    [[nodiscard]] Optional<float> predictLanding(auto&& localPawn, const UserCmd& userCmd, const cs2::Vector& velocity) const noexcept
    {
        auto* const pawnEntity = static_cast<cs2::C_BaseEntity*>(localPawn.baseEntity());
        const auto origin = localPawn.baseEntity().absOrigin();
        const auto tickInterval = hookContext.globalVars().tickInterval();
        const auto hull = collisionHull(localPawn);
        if (!origin.hasValue() || !tickInterval.hasValue() || !hull.hasValue())
            return {};

        const auto svGravity = conVarFloat<cs2::sv_gravity>();
        const auto standableNormal = conVarFloat<cs2::sv_standable_normal>();
        if (!svGravity.hasValue() || !standableNormal.hasValue())
            return {};

        const auto gravityScaleOffset = hookContext.schemaSystem().getFieldOffset("C_BaseEntity", "m_flGravityScale");
        if (!gravityScaleOffset.has_value() || *gravityScaleOffset <= 0)
            return {};
        float gravityScale{};
        std::memcpy(&gravityScale, reinterpret_cast<const std::byte*>(pawnEntity) + *gravityScaleOffset, sizeof(gravityScale));

        cs2::Vector swept = velocity;
        swept.z -= gravityScale * svGravity.value() * tickInterval.value() * 0.5f;

        cs2::Vector end{};
        end.x = origin.value().x + swept.x * tickInterval.value();
        end.y = origin.value().y + swept.y * tickInterval.value();
        end.z = origin.value().z + swept.z * tickInterval.value() - 2.0f;

        const auto result = Tracing::traceHull(origin.value(), end, hull.value().mins, hull.value().maxs, pawnEntity);
        if (result.fraction <= 0.0f || result.fraction >= 1.0f || result.normal.z < standableNormal.value())
            return {};

        
        
        
        if (result.normal.z < 0.985f)
            return {};

        const float grid = result.fraction * 64.0f + 0.5f;
        const float snapped = static_cast<float>(static_cast<int>(grid)) / 64.0f;
        return std::clamp(snapped, 1.0f / 64.0f, 63.0f / 64.0f);
    }

    struct Hull {
        cs2::Vector mins;
        cs2::Vector maxs;
    };

    [[nodiscard]] Optional<Hull> collisionHull(auto&& localPawn) const noexcept
    {
        const auto collisionOffset = hookContext.schemaSystem().getFieldOffset("C_BaseModelEntity", "m_Collision");
        const auto minsOffset = hookContext.schemaSystem().getFieldOffset("CCollisionProperty", "m_vecMins");
        const auto maxsOffset = hookContext.schemaSystem().getFieldOffset("CCollisionProperty", "m_vecMaxs");
        if (!collisionOffset.has_value() || *collisionOffset <= 0
            || !minsOffset.has_value() || *minsOffset <= 0
            || !maxsOffset.has_value() || *maxsOffset <= 0)
            return {};

        const auto* const collision = reinterpret_cast<const std::byte*>(static_cast<cs2::C_BaseEntity*>(localPawn.baseEntity())) + *collisionOffset;
        Hull hull{};
        std::memcpy(&hull.mins, collision + *minsOffset, sizeof(hull.mins));
        std::memcpy(&hull.maxs, collision + *maxsOffset, sizeof(hull.maxs));
        return hull;
    }

    [[nodiscard]] Optional<cs2::Vector> localVelocity(auto&& localPawn) const noexcept
    {
        const auto offset = hookContext.schemaSystem().getFieldOffset("C_BaseEntity", "m_vecAbsVelocity");
        if (!offset.has_value() || *offset <= 0)
            return {};

        cs2::Vector velocity{};
        std::memcpy(&velocity, reinterpret_cast<const std::byte*>(static_cast<cs2::C_BaseEntity*>(localPawn.baseEntity())) + *offset, sizeof(velocity));
        return velocity;
    }

    [[nodiscard]] Optional<std::uint32_t> entityFlags(auto&& localPawn) const noexcept
    {
        const auto offset = hookContext.schemaSystem().getFieldOffset("C_BaseEntity", "m_fFlags");
        if (!offset.has_value() || *offset <= 0)
            return {};
        std::uint32_t flags{};
        std::memcpy(&flags, reinterpret_cast<const std::byte*>(static_cast<cs2::C_BaseEntity*>(localPawn.baseEntity())) + *offset, sizeof(flags));
        return flags;
    }

    [[nodiscard]] Optional<std::uint8_t> moveTypeOf(auto&& localPawn) const noexcept
    {
        const auto offset = hookContext.schemaSystem().getFieldOffset("C_BaseEntity", "m_MoveType");
        if (!offset.has_value() || *offset <= 0)
            return {};
        std::uint8_t moveType{};
        std::memcpy(&moveType, reinterpret_cast<const std::byte*>(static_cast<cs2::C_BaseEntity*>(localPawn.baseEntity())) + *offset, sizeof(moveType));
        return moveType;
    }

    template <typename ConVarType>
    [[nodiscard]] Optional<float> conVarFloat() const noexcept
    {
        const auto value = hookContext.template make<CvarSystem>().template getConVarValue<ConVarType>();
        if (!value.has_value())
            return {};
        return *value;
    }

    
    
    static constexpr std::uint8_t kMoveTypeWalk = 2;
    static constexpr std::uint8_t kMoveTypeLadder = 9;
    static constexpr std::uint32_t kOnGroundFlag = 0x1;
    static constexpr std::uint64_t kJumpButton = 0x2;
    static constexpr std::uint64_t kDuckButton = 0x4;

    inline static bool hasPendingInput{false};
    inline static bool pendingForceMove{false};
    inline static float pendingForward{0.0f};
    inline static float pendingLeft{0.0f};
    inline static bool pendingEdgeJump{false};
    inline static bool pendingJumpBug{false};
    inline static float pendingJumpBugWhen{0.5f};
    inline static bool pendingClearMoveButtons{false};
    inline static bool pendingForwardButton{false};
    inline static bool pendingBackButton{false};
    inline static bool pendingLeftButton{false};
    inline static bool pendingRightButton{false};
    inline static bool pendingViewAngles{false};
    inline static float pendingViewPitch{0.0f};
    inline static float pendingViewYaw{0.0f};

    HookContext& hookContext;
};
