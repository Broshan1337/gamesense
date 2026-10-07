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

        
        
        
        const auto localPredicted = extrapolate(localPawn, localOrigin.value(), kLocalExtrapolationTicks, true);

        steer(userCmd, localPredicted, localOrigin.value(), target, viewYaw.value());
    }

    
    
    
    
    
    
    
    
    
    void onBuildUserCmd(cs2::CCSGOInput* input, int slot) const noexcept
    {
        
        
        if (slot != 0)
            return;

        if (!hasPendingMove || !GET_CONFIG_VAR(BlockbotEnabled) || !KeyboardState::isKeyDown(sdl3::scancode::kE))
            return;

        const CSGOInputMovement movement{input};
        if (!movement)
            return;

        movement.setMove(pendingForward, pendingLeft);
    }

    
    
    
    
    
    
    
    
    
    
    
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

    
    
    
    void onUnload() const noexcept
    {
        stop();
    }

private:
    void stop() const noexcept
    {
        
        
        
        hasPendingMove = false;

        
        
        stickyTargetIndex = kNoTarget;
    }

    
    
    inline static bool hasPendingMove{false};
    inline static float pendingForward{0.0f};
    inline static float pendingLeft{0.0f};

    static constexpr int kNoTarget = -1;

    
    
    
    static constexpr float kSearchForwardOffset = 32.0f;
    static constexpr float kSearchRadius = 256.0f;

    
    
    
    static constexpr float kRetentionRadius = 384.0f;

    static constexpr float kTickInterval = 0.015625f;

    
    
    static constexpr float kLocalExtrapolationTicks = 4.0f;
    static constexpr float kTargetExtrapolationTicks = 2.0f;

    
    static constexpr float kMinimumFrictionSpeed = 0.1f;

    
    
    
    static constexpr float kDeadBandDegrees = 0.5f;

    
    
    static constexpr float kFullDeflection = 1.0f;

    
    
    static constexpr float kOnHeadRadius = 45.0f;

    
    
    
    static constexpr float kFallbackEyeHeight = 64.0f;

    struct Target {
        bool found{false};
        cs2::Vector position{};
        float eyeZ{};
        int entityIndex{kNoTarget};
    };

    
    
    
    
    
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

    
    
    void applyGroundFriction(auto&& pawn, cs2::Vector& velocity) const noexcept
    {
        const float speed = trig::squareRoot(velocity.x * velocity.x + velocity.y * velocity.y);
        if (speed < kMinimumFrictionSpeed)
            return;

        const auto friction = conVarFloat<cs2::sv_friction>();
        const auto stopSpeed = conVarFloat<cs2::sv_stopspeed>();
        if (!friction.hasValue() || !stopSpeed.hasValue())
            return;

        
        
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

    
    
    
    
    
    
    
    void steer(const UserCmd& userCmd, const cs2::Vector& localPredicted, const cs2::Vector& localOrigin, const Target& target, float viewYaw) const noexcept
    {
        const float deltaX = target.position.x - localPredicted.x;
        const float deltaY = target.position.y - localPredicted.y;

        
        
        
        
        
        
        
        
        
        const float bearing = trig::yawTo(deltaX, deltaY);
        const float angleError = trig::normalizeDegrees(bearing - viewYaw);

        
        
        
        
        float desiredLeft = 0.0f;
        if (angleError > kDeadBandDegrees)
            desiredLeft = kFullDeflection;
        else if (angleError < -kDeadBandDegrees)
            desiredLeft = -kFullDeflection;

        
        
        
        
        
        float desiredForward = 0.0f;
        if (isAboveTarget(localOrigin, target)) {
            const float yawRadians = viewYaw * trig::kDegreesToRadians;
            const float forward = trig::sine(yawRadians) * deltaY + trig::cosine(yawRadians) * deltaX;
            desiredForward = saturate(forward);
        }

        
        
        
        
        
        
        
        const auto currentForward = userCmd.forwardMove();
        const auto currentLeft = userCmd.leftMove();
        if (currentForward.hasValue() && currentForward.value() == 0.0f)
            userCmd.setForwardMove(desiredForward);
        if (currentLeft.hasValue() && currentLeft.value() == 0.0f)
            userCmd.setLeftMove(desiredLeft);

        
        
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

    
    
    
    
    inline static int stickyTargetIndex{kNoTarget};

    HookContext& hookContext;
};
