#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>

#include <CS2/Classes/CCSGOInput.h>
#include <CS2/Classes/CUserCmd.h>
#include <Features/Game/Bunnyhop.h>
#include <Features/Game/BunnyhopConfigVariables.h>
#include <Features/Game/Movement.h>
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




























template <typename HookContext>
class TestStrafer {
public:
    explicit TestStrafer(HookContext& hookContext) noexcept
        : hookContext{hookContext}
    {
    }

    
    void onCreateMove(cs2::CUserCmd* cmd) noexcept
    {
        handledThisTick = false;
        captureValid = false;

        const UserCmd userCmd{cmd};
        if (!userCmd)
            return;

        if (!GET_CONFIG_VAR(TestStraferEnabled) || Movement<HookContext>::jumpBugActive)
            return;

        
        auto&& localPawn = hookContext.localPlayerController().pawn().template as<PlayerPawn>();
        if (!localPawn)
            return;
        const auto health = localPawn.health();
        if (!health.hasValue() || health.value() <= 0)
            return;

        
        const auto walking = isWalking(localPawn);
        if (!walking.hasValue() || !walking.value())
            return;

        
        
        const auto onGround = isOnGround(localPawn);
        if (!onGround.hasValue())
            return;
        trackHopSpeed(onGround.value());

        if (onGround.value())
            return;

        const SkipReason reason = capturePath(userCmd);
        if (reason == SkipReason::none)
            captureValid = true;

        
        
        
        
        
        
        bool analogAssist = false;
        if (reason == SkipReason::none) {
            if (const auto dx = userCmd.mouseDx(); dx.hasValue() && dx.value() != 0) {
                const float gainAngle = idealAngle(diagSpeed, capturedTickInterval,
                    capturedMaxSpeed, capturedAirAccelerate, capturedAirMaxWishSpeed, capturedFriction);
                analogAssist = hookContext.template make<Bunnyhop>().engageClassicStrafeAngle(
                    userCmd, gainAngle, dx.value() < 0);
            }
        }

        
        VerifyConsole::write(1.0f, "[strafe]", "r=%s spd=%d max=%.0f q=%d inj=%d a=%d",
            skipReasonText(reason), static_cast<int>(diagSpeed), capturedMaxSpeed, diagQuantized, lastInjectedCount, analogAssist ? 1 : 0);

        if (reason == SkipReason::none)
            return;

        
        
        
        switch (reason) {
        case SkipReason::sprint:
        case SkipReason::quantizeOff:
        case SkipReason::lowSpeed:
            static_cast<void>(hookContext.template make<Bunnyhop>().engageClassicStrafeAngle(
                userCmd, classicAngle(userCmd), strafeSide));
            break;
        default:
            break;
        }
    }

    
    
    
    void onWriteMoveCrc(cs2::CUserCmd* cmd) noexcept
    {
        if (!captureValid)
            return;
        captureValid = false;

        const UserCmd userCmd{cmd};
        std::byte* const base = userCmd.baseMessage();
        if (!base)
            return;

        
        
        const float startWhen = SubtickMoves<HookContext>::maxWhen(base);
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
            
            
            const bool entrySide = ((substepCounter + i) % 2) == 0;

            
            
            
            
            
            
            
            
            const float simSpeed = trig::squareRoot(simVx * simVx + simVy * simVy);
            const float theta = idealAngle(simSpeed, subFrame, capturedMaxSpeed,
                capturedAirAccelerate, capturedAirMaxWishSpeed, capturedFriction);
            const float wishdirYaw = trig::normalizeDegrees(capturedTargetYaw + (entrySide ? theta : -theta));

            
            const float targetViewYaw = trig::normalizeDegrees(wishdirYaw - capturedBaseYawOffset);
            const float yawDelta = trig::normalizeDegrees(targetViewYaw - accumulatedYaw);

            
            
            
            if (trig::absolute(yawDelta) <= 0.01f)
                break;

            const float when = startWhen + static_cast<float>(i) * whenStep;

            auto* const step = hookContext.template make<SubtickMoves>().add(base, when);
            if (!step)
                break;
            
            
            
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

    void onUnload() noexcept
    {
        wasAirborne = false;
        hopPeak = 0.0f;
        lastHopPeak = 0.0f;
        captureValid = false;
        handledThisTick = false;
        lastButtons = 0;
        lastPressed = 0;
    }

private:
    
    
    
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

    
    struct StrafeMove {
        float forward{};
        float left{};
    };

    
    
    [[nodiscard]] SkipReason capturePath(const UserCmd& userCmd) noexcept
    {
        
        
        
        const auto quantized = hookContext.template make<CvarSystem>().readBoolConVar("sv_quantize_movement_input");
        diagQuantized = quantized.has_value() ? (quantized.value() ? 1 : 0) : -1;
        if (quantized.has_value() && !quantized.value())
            return SkipReason::quantizeOff;

        if (userCmd.isButtonDown(kSprintButton))
            return SkipReason::sprint;

        trackMovementKeys(userCmd);

        
        const StrafeMove heldMove = movementFromButtons();
        if (heldMove.forward == 0.0f && heldMove.left == 0.0f)
            return SkipReason::noMovementKeys;

        const auto velocity = localVelocity();
        if (!velocity.hasValue())
            return SkipReason::noVelocity;

        const auto commandYaw = userCmd.viewYaw();
        if (!commandYaw.hasValue())
            return SkipReason::noViewYaw;

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

        
        
        const float friction = surfaceFriction().valueOr(1.0f);

        const auto tickInterval = hookContext.globalVars().tickInterval();
        if (!tickInterval.hasValue() || tickInterval.value() <= 0.0f)
            return SkipReason::noMovementConVars;

        
        
        
        const float baseYawOffset = trig::arcTangent2(-heldMove.left, heldMove.forward) * trig::kRadiansToDegrees;

        
        capturedVx = velocity.value().x;
        capturedVy = velocity.value().y;
        capturedYaw = commandYaw.value();
        capturedTargetYaw = trig::normalizeDegrees(commandYaw.value() + baseYawOffset);
        capturedBaseYawOffset = baseYawOffset;
        capturedAirAccelerate = airAccelerate.value();
        capturedMaxSpeed = effectiveMaxSpeed(maxSpeed.value(), userCmd.isButtonDown(kAttackButton));
        capturedAirMaxWishSpeed = airMaxWishSpeed.value();
        capturedFriction = friction;
        capturedTickInterval = tickInterval.value();
        return SkipReason::none;
    }

    
    
    [[nodiscard]] float classicAngle(const UserCmd& userCmd) noexcept
    {
        const auto velocity = localVelocity();
        const auto airAccelerate = conVarFloat<cs2::sv_airaccelerate>();
        const auto maxSpeed = conVarFloat<cs2::sv_maxspeed>();
        const auto airMaxWishSpeed = conVarFloat<cs2::sv_air_max_wishspeed>();
        const auto tickInterval = hookContext.globalVars().tickInterval();
        if (!velocity.hasValue() || !airAccelerate.hasValue()
            || !maxSpeed.hasValue() || !airMaxWishSpeed.hasValue() || !tickInterval.hasValue())
            return 0.0f;

        const float speed = trig::squareRoot(velocity.value().x * velocity.value().x + velocity.value().y * velocity.value().y);
        const float friction = surfaceFriction().valueOr(1.0f);
        const float angle = idealAngle(speed, tickInterval.value(), effectiveMaxSpeed(maxSpeed.value(), userCmd.isButtonDown(kAttackButton)),
            airAccelerate.value(), airMaxWishSpeed.value(), friction);
        strafeSide = !strafeSide;
        return angle;
    }

    
    
    
    [[nodiscard]] static float idealAngle(float speed, float dt, float wishspeed, float airAccelerate,
        float airMaxWishSpeed, float friction) noexcept
    {
        return air_strafe::idealAngle(speed, {wishspeed, airAccelerate, airMaxWishSpeed, dt, friction}) * trig::kRadiansToDegrees;
    }

    
    
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

    
    
    [[nodiscard]] void* movementServices() const noexcept
    {
        auto&& localPawn = hookContext.localPlayerController().pawn().template as<PlayerPawn>();
        if (!localPawn)
            return nullptr;

        const auto servicesOffset = hookContext.schemaSystem().getFieldOffset("C_BasePlayerPawn", "m_pMovementServices");
        if (!servicesOffset.has_value() || *servicesOffset <= 0)
            return nullptr;

        void* services = nullptr;
        std::memcpy(&services, reinterpret_cast<const std::byte*>(static_cast<cs2::C_BaseEntity*>(localPawn.baseEntity())) + *servicesOffset, sizeof(services));
        return services;
    }

    
    
    [[nodiscard]] Optional<float> surfaceFriction() const noexcept
    {
        void* services = movementServices();
        if (!services)
            return {};

        const auto frictionOffset = hookContext.schemaSystem().getFieldOffset("CCSPlayer_MovementServices", "m_flSurfaceFriction");
        if (!frictionOffset.has_value() || *frictionOffset <= 0)
            return {};

        float friction{};
        std::memcpy(&friction, reinterpret_cast<const std::byte*>(services) + *frictionOffset, sizeof(friction));
        return friction;
    }

    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    
    [[nodiscard]] float effectiveMaxSpeed(float convarMaxSpeed, bool attacking) noexcept
    {
        float maxSpeed = convarMaxSpeed;
        void* services = movementServices();
        if (services) {
            if (const auto maxspeedOffset = hookContext.schemaSystem().getFieldOffset("CCSPlayer_MovementServices", "m_flMaxspeed"); maxspeedOffset.has_value() && *maxspeedOffset > 0) {
                float servicesMax{};
                std::memcpy(&servicesMax, reinterpret_cast<const std::byte*>(services) + *maxspeedOffset, sizeof(servicesMax));
                if (servicesMax > 0.0f && servicesMax < maxSpeed)
                    maxSpeed = servicesMax;
            }
        }

        auto&& localPawn = hookContext.localPlayerController().pawn().template as<PlayerPawn>();
        auto&& weapon = localPawn.getActiveWeapon();
        const auto weaponMax = weapon.maxSpeed();
        if (weaponMax.hasValue() && weaponMax.value() > 0.0f && weaponMax.value() < maxSpeed)
            maxSpeed = weaponMax.value();

        
        
        if (attacking) {
            const auto attackFactor = weapon.attackMovespeedFactor();
            if (attackFactor.hasValue() && attackFactor.value() > 0.0f)
                maxSpeed *= attackFactor.value();
        }

        if (services) {
            if (const auto staminaOffset = hookContext.schemaSystem().getFieldOffset("CCSPlayer_MovementServices", "m_flStamina"); staminaOffset.has_value() && *staminaOffset > 0) {
                float stamina{};
                std::memcpy(&stamina, reinterpret_cast<const std::byte*>(services) + *staminaOffset, sizeof(stamina));
                if (stamina > 0.0f) {
                    const float scale = std::clamp(1.0f - stamina / 100.0f, 0.0f, 1.0f);
                    maxSpeed *= scale * scale;
                }
            }
        }

        
        
        
        
        
        
        
        
        
        const auto legacyJump = hookContext.template make<CvarSystem>().readBoolConVar("sv_legacy_jump");
        if (services && legacyJump.has_value() && !legacyJump.value()) {
            const auto jumpOffset = hookContext.schemaSystem().getFieldOffset("CCSPlayer_MovementServices", "m_ModernJump");
            const auto landedTickOffset = hookContext.schemaSystem().getFieldOffset("CCSPlayerModernJump", "m_nLastLandedTick");
            const auto landedFracOffset = hookContext.schemaSystem().getFieldOffset("CCSPlayerModernJump", "m_flLastLandedFrac");
            const auto landedVelZOffset = hookContext.schemaSystem().getFieldOffset("CCSPlayerModernJump", "m_flLastLandedVelocityZ");
            const auto nowTick = hookContext.globalVars().tickCount();
            if (jumpOffset.has_value() && *jumpOffset > 0
                && landedTickOffset.has_value() && *landedTickOffset > 0
                && landedFracOffset.has_value() && *landedFracOffset > 0
                && landedVelZOffset.has_value() && *landedVelZOffset > 0
                && nowTick.hasValue()) {
                const auto* jump = reinterpret_cast<const std::byte*>(services) + *jumpOffset;
                int landedTick{};
                float landedFrac{};
                float landedVelZ{};
                std::memcpy(&landedTick, jump + *landedTickOffset, sizeof(landedTick));
                std::memcpy(&landedFrac, jump + *landedFracOffset, sizeof(landedFrac));
                std::memcpy(&landedVelZ, jump + *landedVelZOffset, sizeof(landedVelZ));

                const float base = std::clamp(1.0f + landedVelZ * 0.0005f, 0.2f, 1.0f);
                
                
                
                const float elapsed = static_cast<float>(nowTick.value()) - (static_cast<float>(landedTick) + landedFrac);
                const int elapsedTicks = elapsed > 0.0f ? static_cast<int>(elapsed) : 0;
                const float regained = base * base + static_cast<float>(elapsedTicks) * (1.0f / 64.0f) * 1.1111894f;
                if (regained < 1.0f)
                    maxSpeed *= regained;
            }
        }

        return maxSpeed;
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

    [[nodiscard]] Optional<bool> isWalking(auto&& localPawn) const noexcept
    {
        const auto offset = hookContext.schemaSystem().getFieldOffset("C_BaseEntity", "m_MoveType");
        if (!offset.has_value() || *offset <= 0)
            return {};

        std::uint8_t moveType{};
        std::memcpy(&moveType, reinterpret_cast<const std::byte*>(static_cast<cs2::C_BaseEntity*>(localPawn.baseEntity())) + *offset, sizeof(moveType));
        return moveType == kMoveTypeWalk;
    }

    
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

    
    
    static constexpr std::uint64_t kSprintButton = 0x10000;

    
    static constexpr std::uint64_t kAttackButton = 0x1;

    
    inline static bool captureValid{false};

    
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

    
    inline static bool handledThisTick{false};
    inline static std::uint64_t lastButtons{0};
    inline static std::uint64_t lastPressed{0};
    inline static int substepCounter{0};

    
    inline static int lastInjectedCount{0};
    inline static float diagSpeed{0.0f};
    inline static int diagQuantized{-1};

    
    inline static bool wasAirborne{false};
    inline static float hopPeak{0.0f};
    inline static float lastHopPeak{0.0f};

    
    inline static bool strafeSide{false};

    HookContext& hookContext;
};
